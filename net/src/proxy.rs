//! CONNECT tunnel for kernels that can only use an HTTP proxy (the system
//! WebView). Each connection is resolved through the encrypted resolver, so
//! hostnames never leak to the local network's plaintext DNS.

use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use tracing::{debug, info, warn};

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
    let request_line = lines.next().unwrap_or_default().to_string();
    let mut parts = request_line.split_whitespace();
    let method = parts.next().unwrap_or_default().to_string();
    let target = parts.next().unwrap_or_default().to_string();

    if method.eq_ignore_ascii_case("CONNECT") {
        return tunnel(client, &target, &state).await;
    }

    // WebKit sends absolute-form requests for plain http:// URLs through a
    // proxy. Forward them to the origin so those sites keep working.
    if target.starts_with("http://") {
        let pending = buffer[header_end + 4..].to_vec();
        return forward_http(client, &request_line, &head, &target, &pending, &state).await;
    }

    respond(
        &mut client,
        405,
        "Only CONNECT and http:// requests are supported",
    )
    .await
}

async fn connect_upstream(host: &str, port: u16, state: &SharedState) -> Option<TcpStream> {
    for address in resolve_addresses(state, host).await {
        match TcpStream::connect((address, port)).await {
            Ok(stream) => {
                stream.set_nodelay(true).ok();
                return Some(stream);
            }
            Err(error) => debug!(%address, %error, "connect failed"),
        }
    }
    None
}

async fn tunnel(mut client: TcpStream, target: &str, state: &SharedState) -> std::io::Result<()> {
    let (host, port) = match split_host_port(target) {
        Some(value) => value,
        None => return respond(&mut client, 400, "Bad target").await,
    };

    let Some(mut upstream) = connect_upstream(&host, port, state).await else {
        return respond(&mut client, 502, "Cannot resolve or reach the host").await;
    };
    info!(host = %host, port, "CONNECT tunnel established");

    client
        .write_all(b"HTTP/1.1 200 Connection Established\r\n\r\n")
        .await?;
    tokio::io::copy_bidirectional(&mut client, &mut upstream).await?;
    Ok(())
}

async fn forward_http(
    mut client: TcpStream,
    request_line: &str,
    head: &str,
    target: &str,
    pending: &[u8],
    state: &SharedState,
) -> std::io::Result<()> {
    let rest = &target["http://".len()..];
    let (host, port, path) = match split_absolute_url(rest) {
        Some(value) => value,
        None => return respond(&mut client, 400, "Bad URL").await,
    };

    let Some(mut upstream) = connect_upstream(&host, port, state).await else {
        return respond(&mut client, 502, "Cannot resolve or reach the host").await;
    };

    // Rewrite the request line to origin-form and drop proxy-only headers.
    let rewritten = rewrite_head(request_line, &path, head);

    upstream.write_all(rewritten.as_bytes()).await?;
    if !pending.is_empty() {
        upstream.write_all(pending).await?;
    }
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

fn find_header_end(buffer: &[u8]) -> Option<usize> {
    buffer.windows(4).position(|window| window == b"\r\n\r\n")
}

/// Splits `host[:port]/path` (the part after `http://`) into its parts.
pub fn split_absolute_url(rest: &str) -> Option<(String, u16, String)> {
    let (authority, path) = match rest.find('/') {
        Some(index) => (&rest[..index], &rest[index..]),
        None => (rest, "/"),
    };
    if authority.is_empty() {
        return None;
    }
    match split_host_port(authority) {
        Some((host, port)) => Some((host, port, path.to_string())),
        None if !authority.contains(':') => Some((authority.to_string(), 80, path.to_string())),
        None => None,
    }
}

/// Converts a proxy request head to origin-form and removes proxy-only headers.
pub fn rewrite_head(request_line: &str, path: &str, head: &str) -> String {
    let mut out = String::new();
    let mut parts = request_line.split_whitespace();
    let method = parts.next().unwrap_or("GET");
    let version = parts.nth(1).unwrap_or("HTTP/1.1");
    out.push_str(&format!("{method} {path} {version}\r\n"));
    for line in head.split("\r\n").skip(1) {
        if line.is_empty() {
            continue;
        }
        let name = line.split(':').next().unwrap_or("").to_ascii_lowercase();
        if name == "proxy-connection" || name == "proxy-authorization" {
            continue;
        }
        out.push_str(line);
        out.push_str("\r\n");
    }
    out.push_str("\r\n");
    out
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

    #[test]
    fn splits_absolute_urls() {
        assert_eq!(
            split_absolute_url("example.com/path?q=1"),
            Some(("example.com".to_string(), 80, "/path?q=1".to_string()))
        );
        assert_eq!(
            split_absolute_url("example.com:8080"),
            Some(("example.com".to_string(), 8080, "/".to_string()))
        );
        assert_eq!(
            split_absolute_url("example.com"),
            Some(("example.com".to_string(), 80, "/".to_string()))
        );
        assert_eq!(split_absolute_url("/only-path"), None);
    }

    #[test]
    fn rewrites_proxy_requests() {
        let head = "GET http://example.com/a HTTP/1.1\r\nHost: example.com\r\nProxy-Connection: keep-alive\r\nAccept: */*";
        let rewritten = rewrite_head("GET http://example.com/a HTTP/1.1", "/a", head);
        assert!(rewritten.starts_with("GET /a HTTP/1.1\r\n"));
        assert!(rewritten.contains("Host: example.com\r\n"));
        assert!(!rewritten.to_ascii_lowercase().contains("proxy-connection"));
        assert!(rewritten.ends_with("\r\n\r\n"));
    }
}
