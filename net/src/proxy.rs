//! CONNECT tunnel for kernels that can only use an HTTP proxy (the system
//! WebView). Each connection is resolved through the encrypted resolver, so
//! hostnames never leak to the local network's plaintext DNS.

use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use tracing::{debug, warn};

use crate::server::{resolve_addresses, SharedState};

const MAX_HEADER: usize = 16 * 1024;

pub async fn serve(listener: TcpListener, state: SharedState) {
    loop {
        let (stream, peer) = match listener.accept().await {
            Ok(value) => value,
            Err(error) => {
                warn!(%error, "proxy accept failed");
                tokio::time::sleep(std::time::Duration::from_millis(50)).await;
                continue;
            }
        };
        let state = state.clone();
        tokio::spawn(async move {
            if let Err(error) = handle(stream, state).await {
                debug!(%peer, %error, "proxy connection ended");
            }
        });
    }
}

async fn handle(mut client: TcpStream, state: SharedState) -> std::io::Result<()> {
    let mut buffer = Vec::with_capacity(2048);
    let mut chunk = [0u8; 1024];
    let header_end = loop {
        if buffer.len() > MAX_HEADER {
            return respond(&mut client, 431, "Request Header Fields Too Large").await;
        }
        let read = client.read(&mut chunk).await?;
        if read == 0 {
            return Ok(());
        }
        buffer.extend_from_slice(&chunk[..read]);
        if let Some(position) = find_header_end(&buffer) {
            break position;
        }
    };

    let head = String::from_utf8_lossy(&buffer[..header_end]).to_string();
    let mut lines = head.split("\r\n");
    let request_line = lines.next().unwrap_or_default();
    let mut parts = request_line.split_whitespace();
    let method = parts.next().unwrap_or_default();
    let target = parts.next().unwrap_or_default();

    if !method.eq_ignore_ascii_case("CONNECT") {
        return respond(&mut client, 405, "Only CONNECT is supported").await;
    }

    let (host, port) = match split_host_port(target) {
        Some(value) => value,
        None => return respond(&mut client, 400, "Bad target").await,
    };

    let addresses = resolve_addresses(&state, &host).await;
    let mut upstream = None;
    for address in addresses {
        match TcpStream::connect((address, port)).await {
            Ok(stream) => {
                upstream = Some(stream);
                break;
            }
            Err(error) => debug!(%address, %error, "connect failed"),
        }
    }
    let Some(mut upstream) = upstream else {
        return respond(&mut client, 502, "Cannot resolve or reach the host").await;
    };

    client
        .write_all(b"HTTP/1.1 200 Connection Established\r\n\r\n")
        .await?;
    upstream.set_nodelay(true).ok();

    tokio::io::copy_bidirectional(&mut client, &mut upstream).await?;
    Ok(())
}

async fn respond(client: &mut TcpStream, code: u16, reason: &str) -> std::io::Result<()> {
    let body = format!("{code} {reason}\n");
    let response = format!(
        "HTTP/1.1 {code} {reason}\r\ncontent-type: text/plain; charset=utf-8\r\ncontent-length: {}\r\nconnection: close\r\n\r\n{body}",
        body.len()
    );
    client.write_all(response.as_bytes()).await?;
    client.shutdown().await
}

fn find_header_end(buffer: &[u8]) -> Option<usize> {
    buffer.windows(4).position(|window| window == b"\r\n\r\n")
}

pub fn split_host_port(target: &str) -> Option<(String, u16)> {
    if let Some(rest) = target.strip_prefix('[') {
        let (host, tail) = rest.split_once(']')?;
        let port = tail.strip_prefix(':')?.parse().ok()?;
        return Some((format!("[{host}]"), port));
    }
    let (host, port) = target.rsplit_once(':')?;
    if host.is_empty() {
        return None;
    }
    Some((host.to_string(), port.parse().ok()?))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_targets() {
        assert_eq!(
            split_host_port("example.com:443"),
            Some(("example.com".to_string(), 443))
        );
        assert_eq!(
            split_host_port("[2001:db8::1]:8443"),
            Some(("[2001:db8::1]".to_string(), 8443))
        );
        assert_eq!(split_host_port("nope"), None);
        assert_eq!(split_host_port(":443"), None);
    }

    #[test]
    fn finds_header_end() {
        assert_eq!(find_header_end(b"GET / HTTP/1.1\r\n\r\nbody"), Some(14));
        assert_eq!(find_header_end(b"incomplete"), None);
    }
}
