//! Small, bounded, loopback-only client. No TLS/browser/runtime is needed for local IPC.
use serde::Deserialize;
use std::{
    io::{Read, Write},
    net::{Ipv4Addr, SocketAddrV4, TcpStream},
    time::Duration,
};

const MAX_BODY: usize = 2 * 1024 * 1024;

#[derive(Clone)]
pub struct Connection {
    port: u16,
    token: String,
    pub parent: u32,
}

impl std::fmt::Debug for Connection {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Connection")
            .field("parent", &self.parent)
            .finish_non_exhaustive()
    }
}

impl Connection {
    pub fn parse(address: &str, parent: &str) -> Result<Self, String> {
        let invalid = || "Invalid NovaMix launch parameters. Start NovaMix.exe.".to_owned();
        let local = address
            .strip_prefix("http://127.0.0.1:")
            .ok_or_else(invalid)?;
        let (port, token) = local.split_once("/ui?token=").ok_or_else(invalid)?;
        if port.is_empty()
            || !port.bytes().all(|b| b.is_ascii_digit())
            || port.starts_with('0')
            || token.len() != 64
            || !token.bytes().all(|b| b.is_ascii_hexdigit())
            || parent.is_empty()
            || !parent.bytes().all(|b| b.is_ascii_digit())
            || parent.starts_with('0')
        {
            return Err(invalid());
        }
        let port = port.parse::<u16>().map_err(|_| invalid())?;
        let parent = parent.parse::<u32>().map_err(|_| invalid())?;
        if port == 0 || parent == 0 || parent == std::process::id() {
            return Err(invalid());
        }
        Ok(Self {
            port,
            token: token.to_owned(),
            parent,
        })
    }

    pub fn state(&self) -> Result<State, String> {
        serde_json::from_str(&self.request("GET", "/api/state")?)
            .map_err(|_| "Invalid response from the NovaMix engine.".to_owned())
    }

    pub fn action(&self, name: &str, value: i32) -> Result<(), String> {
        // Every name comes from compiled UI code. Validate again at the transport boundary.
        if name.is_empty() || !name.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'-') {
            return Err("Invalid command name.".into());
        }
        self.request("POST", &format!("/api/action?name={name}&value={value}"))
            .map(|_| ())
    }

    pub fn diagnostics(&self) -> Result<String, String> {
        self.request("GET", "/api/diagnostics")
    }

    fn request(&self, method: &str, path: &str) -> Result<String, String> {
        let mut stream = TcpStream::connect_timeout(
            &SocketAddrV4::new(Ipv4Addr::LOCALHOST, self.port).into(),
            Duration::from_millis(900),
        )
        .map_err(|_| {
            "Connection to NovaMix interrupted. Reopen the panel from the tray.".to_owned()
        })?;
        stream
            .set_read_timeout(Some(Duration::from_millis(1200)))
            .map_err(|_| "IPC timeout error.")?;
        stream
            .set_write_timeout(Some(Duration::from_millis(900)))
            .map_err(|_| "IPC timeout error.")?;
        stream.set_nodelay(true).map_err(|_| "IPC socket error.")?;
        write!(stream, "{method} {path} HTTP/1.1\r\nHost: 127.0.0.1:{}\r\nX-NovaMix-Token: {}\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", self.port, self.token)
            .map_err(|_| "Could not send a command to NovaMix.".to_owned())?;
        let mut response = Vec::new();
        stream
            .take((MAX_BODY + 8193) as u64)
            .read_to_end(&mut response)
            .map_err(|_| "NovaMix did not respond in time.".to_owned())?;
        parse_response(&response)
    }
}

fn parse_response(response: &[u8]) -> Result<String, String> {
    let end = response
        .windows(4)
        .position(|w| w == b"\r\n\r\n")
        .ok_or("Invalid IPC response.")?;
    if end > 8192 || response.len() - end - 4 > MAX_BODY {
        return Err("IPC response exceeds size limit.".into());
    }
    let headers = std::str::from_utf8(&response[..end]).map_err(|_| "Invalid IPC headers.")?;
    let mut lines = headers.split("\r\n");
    let status = lines
        .next()
        .and_then(|s| s.strip_prefix("HTTP/1.1 "))
        .and_then(|s| s.split(' ').next())
        .and_then(|s| s.parse::<u16>().ok())
        .ok_or("Invalid IPC status.")?;
    let mut content_length = None;
    for line in lines {
        let (key, value) = line.split_once(':').ok_or("Invalid IPC header.")?;
        if key.eq_ignore_ascii_case("transfer-encoding") {
            return Err("Unexpected IPC transfer encoding.".into());
        }
        if key.eq_ignore_ascii_case("content-length") {
            if content_length.is_some() {
                return Err("Duplicate IPC length.".into());
            }
            content_length = Some(
                value
                    .trim()
                    .parse::<usize>()
                    .map_err(|_| "Invalid IPC length.")?,
            );
        }
    }
    let body = &response[end + 4..];
    if content_length != Some(body.len()) {
        return Err("Incomplete IPC response.".into());
    }
    let body = std::str::from_utf8(body).map_err(|_| "Invalid IPC text.")?;
    if !(200..300).contains(&status) {
        // Only bounded server error text is displayed, never request URLs or tokens.
        return Err(if status == 403 {
            "NovaMix session expired. Reopen the panel from the tray.".into()
        } else {
            body.chars().take(240).collect()
        });
    }
    Ok(body.to_owned())
}

#[derive(Debug, Clone, Default, Deserialize, PartialEq, Eq)]
pub struct Device {
    pub number: i32,
    pub name: String,
}
impl std::fmt::Display for Device {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.name.fmt(f)
    }
}

#[derive(Debug, Clone, Deserialize, PartialEq)]
#[serde(default, rename_all = "camelCase")]
pub struct State {
    pub version: String,
    pub enabled: i32,
    pub running_requested: i32,
    pub accent: i32,
    pub gentle_mix: i32,
    pub audio: i32,
    pub hid: i32,
    pub game: i32,
    pub chat: i32,
    pub balance: i32,
    pub master: i32,
    pub physical: i32,
    pub physical_display: i32,
    pub physical_pending: i32,
    pub mic: i32,
    pub battery: i32,
    pub charger_battery: i32,
    pub charging: i32,
    pub overlay: i32,
    pub keyboard: i32,
    pub keyboard_active: i32,
    pub oled: i32,
    pub oled_mix: i32,
    pub preview: i32,
    pub language: i32,
    pub device: String,
    pub notice: String,
    pub error: String,
    pub hid_error: String,
    pub sync_error: String,
    pub volume_sync_error: String,
    pub route_ready: i32,
    pub routes: [i32; 3],
    pub signal: [i32; 2],
    pub devices: Vec<Device>,
    pub battery_percent: i32,
    pub battery_background: i32,
    pub battery_font_size: i32,
    pub battery_background_size: i32,
    pub battery_notify: i32,
    pub battery_threshold: i32,
    pub battery_low_limit: i32,
    pub battery_medium_limit: i32,
    pub battery_low: i32,
    pub battery_medium: i32,
    pub battery_normal: i32,
}

impl Default for State {
    fn default() -> Self {
        Self {
            version: String::new(),
            enabled: 0,
            running_requested: 0,
            accent: 0,
            gentle_mix: 1,
            audio: 0,
            hid: 0,
            game: 100,
            chat: 100,
            balance: 0,
            master: 100,
            physical: -1,
            physical_display: -1,
            physical_pending: 0,
            mic: -1,
            battery: -1,
            charger_battery: -1,
            charging: 0,
            overlay: 1,
            keyboard: 0,
            keyboard_active: 0,
            oled: 0,
            oled_mix: 1,
            preview: 0,
            language: 0,
            device: String::new(),
            notice: String::new(),
            error: String::new(),
            hid_error: String::new(),
            sync_error: String::new(),
            volume_sync_error: String::new(),
            route_ready: 0,
            routes: [0; 3],
            signal: [0; 2],
            devices: vec![],
            battery_percent: 0,
            battery_background: 1,
            battery_font_size: 12,
            battery_background_size: 16,
            battery_notify: 0,
            battery_threshold: 25,
            battery_low_limit: 25,
            battery_medium_limit: 50,
            battery_low: 0xff8f91,
            battery_medium: 0xffc36d,
            battery_normal: 0x79e4b9,
        }
    }
}

impl State {
    pub fn demo() -> Self {
        Self {
            version: "0.20.12".into(),
            enabled: 1,
            running_requested: 1,
            audio: 1,
            hid: 1,
            game: 75,
            chat: 100,
            balance: 25,
            master: 64,
            physical: 75,
            physical_display: 75,
            mic: 0,
            battery: 75,
            charger_battery: 50,
            charging: 8,
            preview: 1,
            route_ready: 1,
            routes: [1, 2, 3],
            signal: [38, 62],
            devices: vec![
                Device {
                    number: 1,
                    name: "NovaMix PC".into(),
                },
                Device {
                    number: 2,
                    name: "NovaMix Chat".into(),
                },
                Device {
                    number: 3,
                    name: "Arctis Nova Pro Wireless".into(),
                },
            ],
            ..Self::default()
        }
    }
    pub fn apply(&mut self, name: &str, value: i32) {
        match name {
            "balance" => {
                self.balance = value;
                self.game = 100 - value.max(0);
                self.chat = 100 + value.min(0);
            }
            "master" => self.master = value,
            "physical" => {
                self.physical_display = value;
                self.physical_pending = 1;
            }
            "enabled" => self.running_requested = value,
            "language" => self.language = value,
            "accent" => self.accent = value,
            "gentleMix" => self.gentle_mix = value,
            "overlay" => self.overlay = value,
            "keyboard" => self.keyboard = value,
            "oled" => self.oled = value,
            "oledMix" => self.oled_mix = value,
            "batteryPercent" => self.battery_percent = value,
            "batteryBackground" => self.battery_background = value,
            "batteryNotify" => self.battery_notify = value,
            "batteryFontSize" => self.battery_font_size = value,
            "batteryBackgroundSize" => self.battery_background_size = value,
            "batteryThreshold" => self.battery_threshold = value,
            "batteryBands" => {
                self.battery_low_limit = value / 100;
                self.battery_medium_limit = value % 100;
            }
            "batteryLow" => self.battery_low = value,
            "batteryMedium" => self.battery_medium = value,
            "batteryNormal" => self.battery_normal = value,
            "route-pc" => self.routes[0] = value,
            "route-chat" => self.routes[1] = value,
            "route-out" => self.routes[2] = value,
            _ => (),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn launch_is_loopback_only_and_redacted() {
        let address = format!("http://127.0.0.1:45321/ui?token={}", "a".repeat(64));
        assert!(Connection::parse(&address, "12345").is_ok());
        for bad in [
            address.replace("127.0.0.1", "localhost"),
            address.replace("http:", "https:"),
            address.replace(":45321", ":65536"),
            address.replace(":45321", ":045321"),
            format!("{address}#fragment"),
            format!("{address}&extra=1"),
            format!("{address}\r\n"),
            address.replace("127.0.0.1", "user@127.0.0.1"),
        ] {
            assert!(Connection::parse(&bad, "12345").is_err());
        }
        for parent in ["0", "-1", "1234\n", "4294967296", "01", ""] {
            assert!(Connection::parse(&address, parent).is_err());
        }
        assert!(
            !format!("{:?}", Connection::parse(&address, "12345").unwrap())
                .contains(&"a".repeat(64))
        );
    }
    #[test]
    fn response_framing_is_bounded_and_exact() {
        assert_eq!(
            parse_response(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}").unwrap(),
            "{}"
        );
        for bad in [
            b"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\n{}".as_slice(),
            b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n{}",
            b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
            b"HTTP/1.1 302 Redirect\r\nContent-Length: 0\r\n\r\n",
        ] {
            assert!(parse_response(bad).is_err());
        }
    }
}
