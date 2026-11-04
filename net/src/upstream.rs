//! Encrypted upstream transports: DNS over HTTPS and DNS over TLS.
//! DoQ is reported as unsupported rather than silently falling back to TCP.

use std::sync::Arc;
use std::time::Duration;

use rustls::pki_types::ServerName;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;
use tokio_rustls::TlsConnector;

use crate::config::Endpoint;

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("DNS over QUIC is not available in this build")]
    DoqUnsupported,
    #[error("unsupported DNS protocol: {0}")]
    UnsupportedProtocol(String),
    #[error("network error: {0}")]
    Network(String),
    #[error("invalid server name")]
    BadName,
    #[error("upstream returned an unusable answer")]
    BadAnswer,
}

pub struct Resolver {
    http: reqwest::Client,
    tls: TlsConnector,
}

impl Resolver {
    pub fn new() -> Result<Self, Error> {
        let mut roots = rustls::RootCertStore::empty();
        roots.extend(webpki_roots::TLS_SERVER_ROOTS.iter().cloned());
        let tls_config = rustls::ClientConfig::builder()
            .with_root_certificates(roots)
            .with_no_client_auth();
        let http = reqwest::Client::builder()
            .timeout(Duration::from_secs(8))
            .pool_idle_timeout(Duration::from_secs(60))
            .user_agent(concat!("liora-net/", env!("CARGO_PKG_VERSION")))
            .build()
            .map_err(|error| Error::Network(error.to_string()))?;
        Ok(Self {
            http,
            tls: TlsConnector::from(Arc::new(tls_config)),
        })
    }

    /// Sends a raw DNS query and returns the raw response.
    pub async fn query(&self, endpoint: &Endpoint, query: &[u8]) -> Result<Vec<u8>, Error> {
        match endpoint.protocol.as_str() {
            "doh" => self.query_doh(endpoint, query).await,
            "dot" => self.query_dot(endpoint, query).await,
            "doq" => Err(Error::DoqUnsupported),
            other => Err(Error::UnsupportedProtocol(other.to_string())),
        }
    }

    async fn query_doh(&self, endpoint: &Endpoint, query: &[u8]) -> Result<Vec<u8>, Error> {
        let response = self
            .http
            .post(&endpoint.address)
            .header("content-type", "application/dns-message")
            .header("accept", "application/dns-message")
            .body(query.to_vec())
            .send()
            .await
            .map_err(|error| Error::Network(error.to_string()))?;

        if !response.status().is_success() {
            return Err(Error::Network(format!(
                "DoH returned HTTP {}",
                response.status()
            )));
        }
        let body = response
            .bytes()
            .await
            .map_err(|error| Error::Network(error.to_string()))?;
        if body.len() < 12 {
            return Err(Error::BadAnswer);
        }
        Ok(body.to_vec())
    }

    async fn query_dot(&self, endpoint: &Endpoint, query: &[u8]) -> Result<Vec<u8>, Error> {
        let host = endpoint.host();
        let port = endpoint_port(&endpoint.address, endpoint.port());
        let address = self.connect_address(endpoint, &host, port).await?;

        let stream = TcpStream::connect(address)
            .await
            .map_err(|error| Error::Network(error.to_string()))?;
        stream
            .set_nodelay(true)
            .map_err(|error| Error::Network(error.to_string()))?;

        let server_name = ServerName::try_from(host.clone()).map_err(|_| Error::BadName)?;
        let mut tls = self
            .tls
            .connect(server_name, stream)
            .await
            .map_err(|error| Error::Network(error.to_string()))?;

        let mut framed = Vec::with_capacity(query.len() + 2);
        framed.extend_from_slice(&(query.len() as u16).to_be_bytes());
        framed.extend_from_slice(query);
        tls.write_all(&framed)
            .await
            .map_err(|error| Error::Network(error.to_string()))?;

        let mut len_bytes = [0u8; 2];
        tls.read_exact(&mut len_bytes)
            .await
            .map_err(|error| Error::Network(error.to_string()))?;
        let len = u16::from_be_bytes(len_bytes) as usize;
        if !(12..=65535).contains(&len) {
            return Err(Error::BadAnswer);
        }
        let mut response = vec![0u8; len];
        tls.read_exact(&mut response)
            .await
            .map_err(|error| Error::Network(error.to_string()))?;
        Ok(response)
    }

    /// Uses the bootstrap address when one is configured, so resolving the
    /// resolver never depends on the system resolver.
    async fn connect_address(
        &self,
        endpoint: &Endpoint,
        host: &str,
        port: u16,
    ) -> Result<std::net::SocketAddr, Error> {
        for candidate in &endpoint.bootstrap {
            let with_port =
                if candidate.contains(':') && candidate.parse::<std::net::IpAddr>().is_err() {
                    candidate.clone()
                } else {
                    format!("{candidate}:{port}")
                };
            if let Ok(address) = with_port.parse::<std::net::SocketAddr>() {
                return Ok(address);
            }
            if let Ok(ip) = candidate.parse::<std::net::IpAddr>() {
                return Ok(std::net::SocketAddr::new(ip, port));
            }
        }
        let mut addresses = tokio::net::lookup_host((host, port))
            .await
            .map_err(|error| Error::Network(error.to_string()))?;
        addresses
            .next()
            .ok_or_else(|| Error::Network("no address".into()))
    }
}

fn endpoint_port(address: &str, fallback: u16) -> u16 {
    address
        .rsplit_once(':')
        .and_then(|(_, port)| port.parse::<u16>().ok())
        .unwrap_or(fallback)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_ports() {
        assert_eq!(endpoint_port("dns.example:8853", 853), 8853);
        assert_eq!(endpoint_port("dns.example", 853), 853);
    }

    #[test]
    fn extracts_hosts() {
        let doh = Endpoint {
            id: "x".into(),
            name: String::new(),
            protocol: "doh".into(),
            address: "https://dns.example/dns-query".into(),
            bootstrap: vec![],
        };
        assert_eq!(doh.host(), "dns.example");
        assert_eq!(doh.port(), 443);

        let dot = Endpoint {
            protocol: "dot".into(),
            address: "dns.example:853".into(),
            ..doh
        };
        assert_eq!(dot.host(), "dns.example");
        assert_eq!(dot.port(), 853);
    }
}
