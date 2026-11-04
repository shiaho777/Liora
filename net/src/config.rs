//! The configuration document produced by Liora Core (`net.config`).

use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct Endpoint {
    pub id: String,
    #[serde(default)]
    pub name: String,
    /// "doh" | "dot" | "doq"
    pub protocol: String,
    /// DoH: full URL. DoT/DoQ: host[:port].
    pub address: String,
    #[serde(default)]
    pub bootstrap: Vec<String>,
}

impl Endpoint {
    pub fn host(&self) -> String {
        if self.protocol == "doh" {
            self.address
                .trim_start_matches("https://")
                .split('/')
                .next()
                .unwrap_or("")
                .split(':')
                .next()
                .unwrap_or("")
                .to_string()
        } else {
            self.address.split(':').next().unwrap_or("").to_string()
        }
    }

    pub fn port(&self) -> u16 {
        match self.protocol.as_str() {
            "doh" => 443,
            "dot" => 853,
            "doq" => 853,
            _ => 853,
        }
    }
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct Rule {
    pub id: String,
    #[serde(default)]
    pub patterns: Vec<String>,
    /// "off" | "prefer" | "force"
    #[serde(default = "default_h3")]
    pub h3: String,
    #[serde(default)]
    pub alpn: Vec<String>,
    #[serde(default)]
    pub ech: bool,
    #[serde(default, rename = "echSource")]
    pub ech_source: String,
    #[serde(default)]
    pub dns: Option<Endpoint>,
}

fn default_h3() -> String {
    "prefer".to_string()
}

impl Rule {
    pub fn force_h3(&self) -> bool {
        self.h3 == "force"
    }

    pub fn touches_https(&self) -> bool {
        self.h3 != "off" || self.ech
    }

    /// Patterns are `example.com`, `*.example.com` or `=example.com`, always
    /// lowercase and without a trailing dot.
    pub fn matches(&self, host: &str) -> bool {
        for pattern in &self.patterns {
            if let Some(base) = pattern.strip_prefix("*.") {
                if host.len() > base.len()
                    && host.ends_with(base)
                    && host.as_bytes()[host.len() - base.len() - 1] == b'.'
                {
                    return true;
                }
            } else if let Some(exact) = pattern.strip_prefix('=') {
                if host == exact {
                    return true;
                }
            } else if host == pattern || host.ends_with(&format!(".{pattern}")) {
                return true;
            }
        }
        false
    }
}

#[derive(Clone, Debug, Default, Deserialize, Serialize)]
pub struct Config {
    #[serde(default)]
    pub version: u32,
    #[serde(default)]
    pub mode: String,
    #[serde(default)]
    pub upstream: Option<Endpoint>,
    #[serde(default)]
    pub rules: Vec<Rule>,
}

impl Config {
    pub fn parse(text: &str) -> Result<Self, String> {
        let config: Config = serde_json::from_str(text).map_err(|error| error.to_string())?;
        if config.upstream.is_none() && config.rules.iter().all(|rule| rule.dns.is_none()) {
            return Err("no upstream resolver is configured".to_string());
        }
        Ok(config)
    }

    pub fn rule_for(&self, host: &str) -> Option<&Rule> {
        self.rules.iter().find(|rule| rule.matches(host))
    }

    /// The resolver for a host: the rule's own DNS, else the global upstream.
    pub fn endpoint_for(&self, host: &str) -> Option<Endpoint> {
        if let Some(rule) = self.rule_for(host) {
            if let Some(endpoint) = &rule.dns {
                return Some(endpoint.clone());
            }
        }
        self.upstream.clone()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rule(patterns: &[&str]) -> Rule {
        Rule {
            id: "r".into(),
            patterns: patterns.iter().map(|p| p.to_string()).collect(),
            h3: "prefer".into(),
            alpn: vec!["h3".into()],
            ech: true,
            ech_source: String::new(),
            dns: None,
        }
    }

    #[test]
    fn patterns_match_like_the_core_does() {
        let exact = rule(&["=x.com"]);
        assert!(exact.matches("x.com"));
        assert!(!exact.matches("sub.x.com"));

        let tree = rule(&["x.com"]);
        assert!(tree.matches("x.com"));
        assert!(tree.matches("sub.x.com"));
        assert!(!tree.matches("notx.com"));

        let sub = rule(&["*.cdn.com"]);
        assert!(sub.matches("a.cdn.com"));
        assert!(!sub.matches("cdn.com"));
    }

    #[test]
    fn rule_dns_overrides_global() {
        let config = Config {
            version: 1,
            mode: "secure".into(),
            upstream: Some(Endpoint {
                id: "global".into(),
                name: "global".into(),
                protocol: "doh".into(),
                address: "https://dns.example/q".into(),
                bootstrap: vec![],
            }),
            rules: vec![Rule {
                dns: Some(Endpoint {
                    id: "local".into(),
                    name: "local".into(),
                    protocol: "doh".into(),
                    address: "https://private.example/q".into(),
                    bootstrap: vec![],
                }),
                ..rule(&["private.example"])
            }],
        };
        assert_eq!(config.endpoint_for("private.example").unwrap().id, "local");
        assert_eq!(config.endpoint_for("other.example").unwrap().id, "global");
    }

    #[test]
    fn rejects_config_without_upstream() {
        assert!(Config::parse(r#"{"version":1,"rules":[]}"#).is_err());
    }
}
