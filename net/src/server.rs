//! Local DoH endpoint. Chromium (and liora-net's own proxy) talk to it; it
//! applies the per-domain rules and forwards to the encrypted upstream.

use std::convert::Infallible;
use std::net::IpAddr;
use std::sync::Arc;

use base64::Engine;
use bytes::Bytes;
use http_body_util::{BodyExt, Full};
use hyper::body::Incoming;
use hyper::{Request, Response, StatusCode};
use tokio::net::TcpListener;
use tokio_rustls::TlsAcceptor;
use tracing::{debug, warn};

use crate::config::{Config, Rule};
use crate::upstream::Resolver;
use crate::wire::{self, Message, Resource, CLASS_IN, FLAG_QR, TYPE_A, TYPE_AAAA, TYPE_HTTPS};

pub struct State {
    pub config: Config,
    pub resolver: Resolver,
}

pub type SharedState = Arc<State>;

fn server_failure(query: &[u8]) -> Vec<u8> {
    let mut message = Message::parse(query).unwrap_or_default();
    message.flags = (message.flags | FLAG_QR) & !0x000F;
    // SERVFAIL
    message.flags |= 0x0002;
    message.answers.clear();
    message.authority.clear();
    message.additional.clear();
    message.encode()
}

/// Applies the direct rules to an upstream answer.
pub async fn resolve(state: &SharedState, query: &[u8]) -> Vec<u8> {
    let request = match Message::parse(query) {
        Ok(message) => message,
        Err(error) => {
            warn!(%error, "cannot parse query");
            return server_failure(query);
        }
    };
    let Some(question) = request.questions.first().cloned() else {
        return server_failure(query);
    };
    let host = wire::strip_root(&question.name).to_string();

    let Some(endpoint) = state.config.endpoint_for(&host) else {
        warn!(host, "no resolver configured");
        return server_failure(query);
    };

    let upstream = match state.resolver.query(&endpoint, query).await {
        Ok(bytes) => bytes,
        Err(error) => {
            warn!(host, %error, "upstream failed");
            return server_failure(query);
        }
    };

    let mut answer = match Message::parse(&upstream) {
        Ok(message) => message,
        Err(error) => {
            warn!(host, %error, "upstream answer is malformed");
            return server_failure(query);
        }
    };
    answer.id = request.id;

    if question.rtype == TYPE_HTTPS {
        if let Some(rule) = state.config.rule_for(&host).cloned() {
            if rule.touches_https() {
                apply_rule(state, &rule, &host, &mut answer).await;
            }
        }
    }

    answer.encode()
}

async fn apply_rule(state: &SharedState, rule: &Rule, host: &str, answer: &mut Message) {
    let existing = answer
        .answers
        .iter()
        .find(|record| record.rtype == TYPE_HTTPS)
        .map(|record| record.rdata.clone());

    // Keep the server's own ECH config when it published one. Otherwise borrow
    // it from the configured source (cloudflare-ech.com by default).
    let has_ech = existing
        .as_deref()
        .and_then(|rdata| wire::parse_https(rdata).ok())
        .map(|(_, _, params)| params.iter().any(|(key, _)| *key == wire::SVCB_ECH))
        .unwrap_or(false);

    let borrowed_ech = if rule.ech && !has_ech && !rule.ech_source.is_empty() {
        fetch_ech(state, rule, host).await
    } else {
        None
    };

    // When the upstream already carries ECH we leave it untouched.
    let ech = if has_ech { None } else { borrowed_ech };
    if rule.ech && ech.is_none() && !has_ech {
        debug!(host, source = %rule.ech_source, "no ECH config available");
    }

    let shaped = wire::shape_https(existing.as_deref(), &rule.alpn, ech, rule.force_h3());
    let record = Resource {
        name: wire::normalize_name(host),
        rtype: TYPE_HTTPS,
        class: CLASS_IN,
        ttl: existing_ttl(answer).unwrap_or(60),
        rdata: shaped,
    };

    match answer
        .answers
        .iter_mut()
        .find(|entry| entry.rtype == TYPE_HTTPS)
    {
        Some(slot) => slot.rdata = record.rdata,
        None => answer.answers.push(record),
    }
}

fn existing_ttl(answer: &Message) -> Option<u32> {
    answer
        .answers
        .iter()
        .find(|record| record.rtype == TYPE_HTTPS)
        .map(|record| record.ttl)
}

async fn fetch_ech(state: &SharedState, rule: &Rule, host: &str) -> Option<Vec<u8>> {
    let source = if rule.ech_source.is_empty() {
        "cloudflare-ech.com"
    } else {
        rule.ech_source.as_str()
    };
    let endpoint = state.config.endpoint_for(host)?;
    let query = wire::build_query(rand_id(), source, TYPE_HTTPS, true);
    let response = match state.resolver.query(&endpoint, &query).await {
        Ok(bytes) => bytes,
        Err(error) => {
            debug!(%error, source, "cannot fetch the ECH config");
            return None;
        }
    };
    let message = Message::parse(&response).ok()?;
    for record in &message.answers {
        if record.rtype != TYPE_HTTPS {
            continue;
        }
        if let Ok((_, _, params)) = wire::parse_https(&record.rdata) {
            for (key, value) in params {
                if key == wire::SVCB_ECH && !value.is_empty() {
                    return Some(value);
                }
            }
        }
    }
    None
}

fn rand_id() -> u16 {
    use std::time::{SystemTime, UNIX_EPOCH};
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.subsec_nanos())
        .unwrap_or(0);
    (nanos & 0xFFFF) as u16
}

// ---- HTTP server -------------------------------------------------------------

pub async fn handle_doh(
    state: SharedState,
    request: Request<Incoming>,
) -> Result<Response<Full<Bytes>>, Infallible> {
    let method = request.method().clone();
    let query = request.uri().query().unwrap_or("");

    let body = match method {
        hyper::Method::GET => {
            let encoded = query
                .split('&')
                .find_map(|pair| pair.strip_prefix("dns="))
                .unwrap_or("");
            match base64::engine::general_purpose::URL_SAFE_NO_PAD.decode(encoded) {
                Ok(bytes) => bytes,
                Err(_) => return Ok(bad_request("invalid dns parameter")),
            }
        }
        hyper::Method::POST => match request.into_body().collect().await {
            Ok(collected) => collected.to_bytes().to_vec(),
            Err(_) => return Ok(bad_request("cannot read the body")),
        },
        _ => return Ok(bad_request("only GET and POST are supported")),
    };

    if body.len() < 12 {
        return Ok(bad_request("the query is too short"));
    }

    let response = resolve(&state, &body).await;
    Ok(Response::builder()
        .status(StatusCode::OK)
        .header("content-type", "application/dns-message")
        .header("cache-control", "no-store")
        .body(Full::new(Bytes::from(response)))
        .unwrap())
}

fn bad_request(message: &str) -> Response<Full<Bytes>> {
    Response::builder()
        .status(StatusCode::BAD_REQUEST)
        .header("content-type", "text/plain; charset=utf-8")
        .body(Full::new(Bytes::from(message.to_string())))
        .unwrap()
}

pub async fn serve(listener: TcpListener, tls: TlsAcceptor, state: SharedState) {
    loop {
        let (stream, peer) = match listener.accept().await {
            Ok(value) => value,
            Err(error) => {
                warn!(%error, "accept failed");
                tokio::time::sleep(std::time::Duration::from_millis(50)).await;
                continue;
            }
        };
        let tls = tls.clone();
        let state = state.clone();
        tokio::spawn(async move {
            let stream = match tls.accept(stream).await {
                Ok(stream) => stream,
                Err(error) => {
                    debug!(%peer, %error, "TLS handshake failed");
                    return;
                }
            };
            let service = hyper::service::service_fn(move |request| {
                let state = state.clone();
                handle_doh(state, request)
            });
            let io = hyper_util::rt::TokioIo::new(stream);
            if let Err(error) =
                hyper_util::server::conn::auto::Builder::new(hyper_util::rt::TokioExecutor::new())
                    .serve_connection(io, service)
                    .await
            {
                debug!(%peer, %error, "connection closed");
            }
        });
    }
}

/// Resolves a host to addresses using the configured encrypted resolver.
/// Used by the CONNECT proxy so WebView traffic never falls back to plaintext.
pub async fn resolve_addresses(state: &SharedState, host: &str) -> Vec<IpAddr> {
    let mut out = Vec::new();
    for rtype in [TYPE_A, TYPE_AAAA] {
        let Some(endpoint) = state.config.endpoint_for(host) else {
            break;
        };
        let query = wire::build_query(rand_id(), host, rtype, true);
        if let Ok(bytes) = state.resolver.query(&endpoint, &query).await {
            if let Ok(message) = Message::parse(&bytes) {
                out.extend(message.ips());
            }
        }
    }
    out
}
