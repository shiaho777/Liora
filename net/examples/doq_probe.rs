//! Live DoQ probe: sends one A query to a real resolver over QUIC and prints
//! the answer. Run with: cargo run --example doq_probe
//!
//! The modules are included directly because liora-net is a binary crate.
//! They carry more than this probe uses, so dead-code lints are relaxed here
//! only — the crate itself stays strict.
#![allow(dead_code)]

#[path = "../src/config.rs"]
mod config;
#[path = "../src/upstream.rs"]
mod upstream;
#[path = "../src/wire.rs"]
mod wire;

use config::Endpoint;
use upstream::Resolver;

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("quinn=debug,info")),
        )
        .with_writer(std::io::stderr)
        .init();

    let targets: [(&str, &str, &[&str]); 2] = [
        (
            "AdGuard",
            "dns.adguard-dns.com",
            &["94.140.14.14", "94.140.15.15"],
        ),
        ("AliDNS", "dns.alidns.com", &["223.5.5.5", "223.6.6.6"]),
    ];

    let resolver = match Resolver::new() {
        Ok(resolver) => resolver,
        Err(error) => {
            eprintln!("resolver init failed: {error}");
            std::process::exit(1);
        }
    };

    let query = wire::build_query(0x1234, "example.com", wire::TYPE_A, true);
    let mut any_ok = false;

    for (label, host, bootstrap) in targets {
        let endpoint = Endpoint {
            id: label.to_lowercase(),
            name: label.to_string(),
            protocol: "doq".into(),
            address: format!("{host}:853"),
            bootstrap: bootstrap.iter().map(|item| item.to_string()).collect(),
        };
        match resolver.query(&endpoint, &query).await {
            Ok(response) => {
                let parsed = wire::Message::parse(&response);
                let addresses: Vec<String> = match &parsed {
                    Ok(message) => message
                        .answers
                        .iter()
                        .filter(|record| record.rtype == wire::TYPE_A && record.rdata.len() == 4)
                        .map(|record| {
                            format!(
                                "{}.{}.{}.{}",
                                record.rdata[0], record.rdata[1], record.rdata[2], record.rdata[3]
                            )
                        })
                        .collect(),
                    Err(_) => vec![],
                };
                println!(
                    "{label}: OK, {} bytes, answers: {:?}",
                    response.len(),
                    addresses
                );
                any_ok = true;
            }
            Err(error) => eprintln!("{label}: FAILED ({error})"),
        }
    }

    if !any_ok {
        std::process::exit(2);
    }
}
