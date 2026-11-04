//! Minimal DNS wire codec: enough to forward queries, inspect answers and
//! rewrite HTTPS (SVCB) records. Names are parsed with compression and written
//! without it, which is always valid.

use std::fmt;

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("truncated message")]
    Truncated,
    #[error("invalid name encoding")]
    InvalidName,
    #[error("compression pointer loop")]
    PointerLoop,
}

#[derive(Clone, Debug, Default)]
pub struct Question {
    pub name: String,
    pub rtype: u16,
    pub class: u16,
}

#[derive(Clone, Debug)]
pub struct Resource {
    pub name: String,
    pub rtype: u16,
    pub class: u16,
    pub ttl: u32,
    pub rdata: Vec<u8>,
}

#[derive(Clone, Debug, Default)]
pub struct Message {
    pub id: u16,
    pub flags: u16,
    pub questions: Vec<Question>,
    pub answers: Vec<Resource>,
    pub authority: Vec<Resource>,
    pub additional: Vec<Resource>,
}

pub const TYPE_A: u16 = 1;
pub const TYPE_AAAA: u16 = 28;
pub const TYPE_HTTPS: u16 = 65;
pub const CLASS_IN: u16 = 1;

pub const FLAG_QR: u16 = 0x8000;
pub const FLAG_RD: u16 = 0x0100;

pub const SVCB_ALPN: u16 = 1;
pub const SVCB_NO_DEFAULT_ALPN: u16 = 2;
pub const SVCB_ECH: u16 = 5;

impl Question {
    pub fn new(name: &str, rtype: u16) -> Self {
        Self {
            name: normalize_name(name),
            rtype,
            class: CLASS_IN,
        }
    }
}

/// Lowercases a name and guarantees a trailing dot, the canonical form used on
/// the wire and for rule matching.
pub fn normalize_name(name: &str) -> String {
    let trimmed = name.trim().trim_end_matches('.');
    let mut out = trimmed.to_ascii_lowercase();
    out.push('.');
    out
}

pub fn strip_root(name: &str) -> &str {
    name.strip_suffix('.').unwrap_or(name)
}

pub fn build_query(id: u16, name: &str, rtype: u16, recursion_desired: bool) -> Vec<u8> {
    let message = Message {
        id,
        flags: if recursion_desired { FLAG_RD } else { 0 },
        questions: vec![Question::new(name, rtype)],
        ..Default::default()
    };
    message.encode()
}

impl Message {
    pub fn parse(bytes: &[u8]) -> Result<Self, Error> {
        let mut reader = Reader::new(bytes);
        let id = reader.u16()?;
        let flags = reader.u16()?;
        let qdcount = reader.u16()?;
        let ancount = reader.u16()?;
        let nscount = reader.u16()?;
        let arcount = reader.u16()?;

        let mut message = Message {
            id,
            flags,
            ..Default::default()
        };

        for _ in 0..qdcount {
            let name = reader.name()?;
            let rtype = reader.u16()?;
            let class = reader.u16()?;
            message.questions.push(Question { name, rtype, class });
        }
        message.answers = reader.records(ancount)?;
        message.authority = reader.records(nscount)?;
        message.additional = reader.records(arcount)?;
        Ok(message)
    }

    pub fn encode(&self) -> Vec<u8> {
        let mut out = Vec::with_capacity(512);
        out.extend_from_slice(&self.id.to_be_bytes());
        out.extend_from_slice(&self.flags.to_be_bytes());
        out.extend_from_slice(&(self.questions.len() as u16).to_be_bytes());
        out.extend_from_slice(&(self.answers.len() as u16).to_be_bytes());
        out.extend_from_slice(&(self.authority.len() as u16).to_be_bytes());
        out.extend_from_slice(&(self.additional.len() as u16).to_be_bytes());

        for question in &self.questions {
            write_name(&mut out, &question.name);
            out.extend_from_slice(&question.rtype.to_be_bytes());
            out.extend_from_slice(&question.class.to_be_bytes());
        }
        for record in self
            .answers
            .iter()
            .chain(self.authority.iter())
            .chain(self.additional.iter())
        {
            write_name(&mut out, &record.name);
            out.extend_from_slice(&record.rtype.to_be_bytes());
            out.extend_from_slice(&record.class.to_be_bytes());
            out.extend_from_slice(&record.ttl.to_be_bytes());
            out.extend_from_slice(&(record.rdata.len() as u16).to_be_bytes());
            out.extend_from_slice(&record.rdata);
        }
        out
    }

    pub fn ips(&self) -> Vec<std::net::IpAddr> {
        let mut out = Vec::new();
        for record in &self.answers {
            match record.rtype {
                TYPE_A if record.rdata.len() == 4 => {
                    out.push(std::net::IpAddr::V4(std::net::Ipv4Addr::new(
                        record.rdata[0],
                        record.rdata[1],
                        record.rdata[2],
                        record.rdata[3],
                    )));
                }
                TYPE_AAAA if record.rdata.len() == 16 => {
                    let mut octets = [0u8; 16];
                    octets.copy_from_slice(&record.rdata);
                    out.push(std::net::IpAddr::V6(std::net::Ipv6Addr::from(octets)));
                }
                _ => {}
            }
        }
        out
    }
}

fn write_name(out: &mut Vec<u8>, name: &str) {
    for label in strip_root(name).split('.') {
        if label.is_empty() {
            continue;
        }
        let bytes = label.as_bytes();
        let len = bytes.len().min(63);
        out.push(len as u8);
        out.extend_from_slice(&bytes[..len]);
    }
    out.push(0);
}

struct Reader<'a> {
    bytes: &'a [u8],
    pos: usize,
}

impl<'a> Reader<'a> {
    fn new(bytes: &'a [u8]) -> Self {
        Self { bytes, pos: 0 }
    }

    fn u8(&mut self) -> Result<u8, Error> {
        let value = *self.bytes.get(self.pos).ok_or(Error::Truncated)?;
        self.pos += 1;
        Ok(value)
    }

    fn u16(&mut self) -> Result<u16, Error> {
        let hi = self.u8()? as u16;
        let lo = self.u8()? as u16;
        Ok((hi << 8) | lo)
    }

    fn u32(&mut self) -> Result<u32, Error> {
        let hi = self.u16()? as u32;
        let lo = self.u16()? as u32;
        Ok((hi << 16) | lo)
    }

    fn take(&mut self, len: usize) -> Result<&'a [u8], Error> {
        let end = self.pos.checked_add(len).ok_or(Error::Truncated)?;
        let slice = self.bytes.get(self.pos..end).ok_or(Error::Truncated)?;
        self.pos = end;
        Ok(slice)
    }

    fn name(&mut self) -> Result<String, Error> {
        let mut labels: Vec<String> = Vec::new();
        let mut pos = self.pos;
        let mut jumped = false;
        let mut hops = 0;

        loop {
            if hops > 64 {
                return Err(Error::PointerLoop);
            }
            let len = *self.bytes.get(pos).ok_or(Error::Truncated)?;
            if len == 0 {
                if !jumped {
                    self.pos = pos + 1;
                }
                break;
            }
            if len & 0xC0 == 0xC0 {
                let low = *self.bytes.get(pos + 1).ok_or(Error::Truncated)? as usize;
                let target = (((len & 0x3F) as usize) << 8) | low;
                if target >= self.bytes.len() {
                    return Err(Error::InvalidName);
                }
                if !jumped {
                    self.pos = pos + 2;
                }
                jumped = true;
                pos = target;
                hops += 1;
                continue;
            }
            if len & 0xC0 != 0 {
                return Err(Error::InvalidName);
            }
            let len = len as usize;
            let start = pos + 1;
            let end = start.checked_add(len).ok_or(Error::Truncated)?;
            let label = self.bytes.get(start..end).ok_or(Error::Truncated)?;
            labels.push(String::from_utf8_lossy(label).into_owned());
            pos = end;
        }

        let mut name = labels.join(".");
        name.push('.');
        Ok(name)
    }

    fn records(&mut self, count: u16) -> Result<Vec<Resource>, Error> {
        let mut out = Vec::with_capacity(count as usize);
        for _ in 0..count {
            let name = self.name()?;
            let rtype = self.u16()?;
            let class = self.u16()?;
            let ttl = self.u32()?;
            let len = self.u16()? as usize;
            let rdata = self.take(len)?.to_vec();
            out.push(Resource {
                name,
                rtype,
                class,
                ttl,
                rdata,
            });
        }
        Ok(out)
    }
}

// ---- SVCB / HTTPS parameters -------------------------------------------------

pub type HttpsParams = Vec<(u16, Vec<u8>)>;
/// Priority, target and raw (key, value) pairs of an HTTPS record.
pub type HttpsRecord = (u16, String, HttpsParams);

/// Parses the SvcParams of an HTTPS record into raw (key, value) pairs.
pub fn parse_https(rdata: &[u8]) -> Result<HttpsRecord, Error> {
    let mut reader = Reader {
        bytes: rdata,
        pos: 0,
    };
    let priority = reader.u16()?;
    let target = reader.name()?;
    let mut params = Vec::new();
    while reader.pos < rdata.len() {
        let key = reader.u16()?;
        let len = reader.u16()? as usize;
        let value = reader.take(len)?.to_vec();
        params.push((key, value));
    }
    Ok((priority, target, params))
}

pub fn build_https(priority: u16, target: &str, params: &[(u16, Vec<u8>)]) -> Vec<u8> {
    let mut out = Vec::new();
    out.extend_from_slice(&priority.to_be_bytes());
    write_name(&mut out, &normalize_name(target));
    for (key, value) in params {
        out.extend_from_slice(&key.to_be_bytes());
        out.extend_from_slice(&(value.len() as u16).to_be_bytes());
        out.extend_from_slice(value);
    }
    out
}

/// ALPN values are length-prefixed inside the parameter value.
pub fn encode_alpn(values: &[String]) -> Vec<u8> {
    let mut out = Vec::new();
    for value in values {
        let bytes = value.as_bytes();
        let len = bytes.len().min(255);
        out.push(len as u8);
        out.extend_from_slice(&bytes[..len]);
    }
    out
}

#[allow(dead_code)] // used by tests and future diagnostics
pub fn decode_alpn(value: &[u8]) -> Vec<String> {
    let mut out = Vec::new();
    let mut pos = 0;
    while pos < value.len() {
        let len = value[pos] as usize;
        pos += 1;
        let end = (pos + len).min(value.len());
        if end > pos {
            out.push(String::from_utf8_lossy(&value[pos..end]).into_owned());
        }
        pos = end;
    }
    out
}

/// Ensures the HTTPS record advertises the wanted ALPN list and carries an ECH
/// config. Existing parameters (IPv4/IPv6 hints, port) are preserved.
pub fn shape_https(
    rdata: Option<&[u8]>,
    alpn: &[String],
    ech: Option<Vec<u8>>,
    force_h3_only: bool,
) -> Vec<u8> {
    let (priority, target, mut params) = match rdata.and_then(|bytes| parse_https(bytes).ok()) {
        Some((priority, target, params)) => (priority, target, params),
        None => (1, String::from("."), Vec::new()),
    };

    let mut alpn_list = alpn.to_vec();
    if force_h3_only {
        alpn_list = vec!["h3".to_string()];
    }
    alpn_list.retain(|item| !item.is_empty());
    if alpn_list.is_empty() {
        alpn_list.push("h2".to_string());
    }

    let mut written_alpn = false;
    let mut written_ech = false;
    for (key, value) in params.iter_mut() {
        match *key {
            SVCB_ALPN => {
                *value = encode_alpn(&alpn_list);
                written_alpn = true;
            }
            SVCB_ECH => {
                if let Some(config) = &ech {
                    *value = config.clone();
                    written_ech = true;
                }
            }
            _ => {}
        }
    }
    if !written_alpn {
        params.push((SVCB_ALPN, encode_alpn(&alpn_list)));
    }
    if let Some(config) = ech {
        if !written_ech && !config.is_empty() {
            params.push((SVCB_ECH, config));
        }
    }
    // A forced-H3 record must not advertise other protocols.
    if force_h3_only {
        params.retain(|(key, _)| *key != SVCB_NO_DEFAULT_ALPN);
    }

    build_https(priority, &target, &params)
}

impl fmt::Display for Message {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "id={} q={} answers={}",
            self.id,
            self.questions
                .first()
                .map(|q| q.name.as_str())
                .unwrap_or("-"),
            self.answers.len()
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn query_roundtrip() {
        let bytes = build_query(0x1234, "Example.COM", TYPE_HTTPS, true);
        let message = Message::parse(&bytes).unwrap();
        assert_eq!(message.id, 0x1234);
        assert_eq!(message.questions.len(), 1);
        assert_eq!(message.questions[0].name, "example.com.");
        assert_eq!(message.questions[0].rtype, TYPE_HTTPS);
        assert!(message.flags & FLAG_RD != 0);
    }

    #[test]
    fn parses_compressed_names() {
        // Header + one question + one answer whose name is a pointer to offset 12.
        let mut bytes = Vec::new();
        bytes.extend_from_slice(&1u16.to_be_bytes());
        bytes.extend_from_slice(&(FLAG_QR | FLAG_RD).to_be_bytes());
        bytes.extend_from_slice(&1u16.to_be_bytes());
        bytes.extend_from_slice(&1u16.to_be_bytes());
        bytes.extend_from_slice(&0u16.to_be_bytes());
        bytes.extend_from_slice(&0u16.to_be_bytes());
        bytes.extend_from_slice(&[
            7, b'e', b'x', b'a', b'm', b'p', b'l', b'e', 3, b'c', b'o', b'm', 0,
        ]);
        bytes.extend_from_slice(&TYPE_A.to_be_bytes());
        bytes.extend_from_slice(&CLASS_IN.to_be_bytes());
        bytes.extend_from_slice(&0xC00Cu16.to_be_bytes());
        bytes.extend_from_slice(&TYPE_A.to_be_bytes());
        bytes.extend_from_slice(&CLASS_IN.to_be_bytes());
        bytes.extend_from_slice(&60u32.to_be_bytes());
        bytes.extend_from_slice(&4u16.to_be_bytes());
        bytes.extend_from_slice(&[1, 2, 3, 4]);

        let message = Message::parse(&bytes).unwrap();
        assert_eq!(message.answers.len(), 1);
        assert_eq!(message.answers[0].name, "example.com.");
        assert_eq!(
            message.ips(),
            vec![std::net::IpAddr::V4(std::net::Ipv4Addr::new(1, 2, 3, 4))]
        );
    }

    #[test]
    fn https_shape_adds_alpn_and_ech() {
        let shaped = shape_https(
            None,
            &["h3".into(), "h2".into()],
            Some(vec![0xAA, 0xBB]),
            false,
        );
        let (priority, target, params) = parse_https(&shaped).unwrap();
        assert_eq!(priority, 1);
        assert_eq!(target, ".");
        let alpn = params.iter().find(|(k, _)| *k == SVCB_ALPN).unwrap();
        assert_eq!(
            decode_alpn(&alpn.1),
            vec!["h3".to_string(), "h2".to_string()]
        );
        let ech = params.iter().find(|(k, _)| *k == SVCB_ECH).unwrap();
        assert_eq!(ech.1, vec![0xAA, 0xBB]);
    }

    #[test]
    fn https_shape_force_keeps_only_h3() {
        let original = build_https(
            1,
            ".",
            &[(SVCB_ALPN, encode_alpn(&["h2".into(), "h3".into()]))],
        );
        let shaped = shape_https(Some(&original), &["h2".into()], Some(vec![1]), true);
        let (_, _, params) = parse_https(&shaped).unwrap();
        let alpn = params.iter().find(|(k, _)| *k == SVCB_ALPN).unwrap();
        assert_eq!(decode_alpn(&alpn.1), vec!["h3".to_string()]);
    }

    #[test]
    fn https_shape_preserves_hints() {
        let original = build_https(1, ".", &[(3, 443u16.to_be_bytes().to_vec())]);
        let shaped = shape_https(Some(&original), &["h3".into()], None, false);
        let (_, _, params) = parse_https(&shaped).unwrap();
        assert!(params.iter().any(|(k, _)| *k == 3));
        assert!(params.iter().any(|(k, _)| *k == SVCB_ALPN));
    }
}
