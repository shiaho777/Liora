//! liora-net: the local resolver and proxy behind Liora's DNS and direct rules.
//!
//! Startup prints one JSON line describing the ports and the certificate key,
//! which the shell passes to Core (`net.setRuntime`):
//!   {"dohPort":5399,"proxyPort":5400,"spki":"..."}

mod config;
mod proxy;
mod server;
mod upstream;
mod wire;

use std::path::PathBuf;
use std::sync::Arc;

use base64::Engine;
use sha2::{Digest, Sha256};
use tokio::net::TcpListener;
use tokio_rustls::TlsAcceptor;
use tracing::{error, info};

use crate::config::Config;
use crate::server::SharedState;
use crate::upstream::Resolver;

struct Args {
    config: PathBuf,
    host: String,
    doh_port: u16,
    proxy_port: u16,
    state_file: Option<PathBuf>,
    no_proxy: bool,
}

fn parse_args() -> Result<Args, String> {
    let mut args = Args {
        config: PathBuf::from("liora-net.json"),
        host: "127.0.0.1".to_string(),
        doh_port: 0,
        proxy_port: 0,
        state_file: None,
        no_proxy: false,
    };
    let mut iter = std::env::args().skip(1);
    while let Some(flag) = iter.next() {
        let mut value = || iter.next().ok_or_else(|| format!("{flag} needs a value"));
        match flag.as_str() {
            "--config" => args.config = PathBuf::from(value()?),
            "--host" => args.host = value()?,
            "--doh-port" => args.doh_port = value()?.parse().map_err(|_| "invalid DoH port")?,
            "--proxy-port" => args.proxy_port = value()?.parse().map_err(|_| "invalid proxy port")?,
            "--state-file" => args.state_file = Some(PathBuf::from(value()?)),
            "--no-proxy" => args.no_proxy = true,
            "--help" | "-h" => return Err("usage: liora-net --config <file> [--doh-port N] [--proxy-port N] [--state-file <file>] [--no-proxy]".into()),
            other => return Err(format!("unknown option: {other}")),
        }
    }
    Ok(args)
}

/// Generates the certificate for the local DoH endpoint. Only its public key is
/// trusted by the kernel (--ignore-certificate-errors-spki-list), so a new key
/// on every start is safe and avoids storing secrets.
fn local_tls() -> Result<(TlsAcceptor, String), String> {
    let names = vec!["127.0.0.1".to_string(), "localhost".to_string()];
    let generated = rcgen::generate_simple_self_signed(names).map_err(|error| error.to_string())?;
    let cert_der = generated.cert.der().to_vec();
    let key_der = generated.key_pair.serialize_der();

    let (_, parsed) =
        x509_parser::parse_x509_certificate(&cert_der).map_err(|error| error.to_string())?;
    let mut hasher = Sha256::new();
    hasher.update(parsed.subject_pki.raw);
    let spki = base64::engine::general_purpose::STANDARD.encode(hasher.finalize());

    let config = rustls::ServerConfig::builder()
        .with_no_client_auth()
        .with_single_cert(
            vec![rustls::pki_types::CertificateDer::from(cert_der)],
            rustls::pki_types::PrivateKeyDer::Pkcs8(rustls::pki_types::PrivatePkcs8KeyDer::from(
                key_der,
            )),
        )
        .map_err(|error| error.to_string())?;
    Ok((TlsAcceptor::from(Arc::new(config)), spki))
}

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_env("LIORA_NET_LOG")
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("warn")),
        )
        .with_writer(std::io::stderr)
        .init();

    let args = match parse_args() {
        Ok(args) => args,
        Err(message) => {
            eprintln!("{message}");
            std::process::exit(2);
        }
    };

    let text = match std::fs::read_to_string(&args.config) {
        Ok(text) => text,
        Err(error) => {
            error!(path = %args.config.display(), %error, "cannot read the configuration");
            std::process::exit(1);
        }
    };
    let config = match Config::parse(&text) {
        Ok(config) => config,
        Err(message) => {
            error!(%message, "invalid configuration");
            std::process::exit(1);
        }
    };

    let resolver = match Resolver::new() {
        Ok(resolver) => resolver,
        Err(error) => {
            error!(%error, "cannot create the resolver");
            std::process::exit(1);
        }
    };

    let state: SharedState = Arc::new(server::State {
        config: config.clone(),
        resolver,
    });

    let (tls, spki) = match local_tls() {
        Ok(value) => value,
        Err(message) => {
            error!(%message, "cannot create the local certificate");
            std::process::exit(1);
        }
    };

    let doh_listener = match TcpListener::bind((args.host.as_str(), args.doh_port)).await {
        Ok(listener) => listener,
        Err(error) => {
            error!(%error, "cannot bind the DoH port");
            std::process::exit(1);
        }
    };
    let doh_port = doh_listener.local_addr().map(|a| a.port()).unwrap_or(0);

    let proxy_listener = if args.no_proxy {
        None
    } else {
        match TcpListener::bind((args.host.as_str(), args.proxy_port)).await {
            Ok(listener) => Some(listener),
            Err(error) => {
                error!(%error, "cannot bind the proxy port");
                std::process::exit(1);
            }
        }
    };
    let proxy_port = proxy_listener
        .as_ref()
        .and_then(|listener| listener.local_addr().ok())
        .map(|address| address.port())
        .unwrap_or(0);

    let state_line =
        format!(r#"{{"dohPort":{doh_port},"proxyPort":{proxy_port},"spki":"{spki}"}}"#);
    if let Some(path) = &args.state_file {
        let _ = std::fs::write(path, &state_line);
    }
    // The shell parses this line from stdout.
    println!("{state_line}");
    use std::io::Write;
    let _ = std::io::stdout().flush();

    info!(
        doh_port,
        proxy_port,
        rules = config.rules.len(),
        "liora-net ready"
    );

    let doh_state = state.clone();
    tokio::spawn(server::serve(doh_listener, tls, doh_state));

    if let Some(listener) = proxy_listener {
        tokio::spawn(proxy::serve(listener, state));
    }

    tokio::signal::ctrl_c().await.ok();
    info!("shutting down");
}
