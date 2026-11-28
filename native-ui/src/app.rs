use crate::{
    protocol::{Connection, Device, State},
    worker::{Event, Worker},
};
use iced::{
    Alignment, Border, Color, Element, Fill, Font, Size, Task, Theme,
    widget::{
        Space, button, column, container, operation, pick_list, progress_bar, row,
        rule::horizontal as horizontal_rule, scrollable, slider, text, text_input, toggler,
    },
    window,
};
use std::{
    path::PathBuf,
    time::{Duration, Instant},
};

const MUTED: Color = Color::from_rgb(0.60, 0.65, 0.68);
const SURFACE: Color = Color::from_rgb(0.098, 0.110, 0.122);
const LINE: Color = Color::from_rgb(0.17, 0.19, 0.21);
const COLOR_KEYS: [&str; 3] = ["batteryLow", "batteryMedium", "batteryNormal"];

#[derive(Default)]
pub struct Launch {
    pub connection: Option<Connection>,
    pub demo: bool,
    pub verify: bool,
    pub result: Option<PathBuf>,
    capture: Option<PathBuf>,
    benchmark: Option<PathBuf>,
    width: u32,
}
impl Launch {
    pub fn parse(args: Vec<String>) -> Result<Self, String> {
        let mut launch = Self {
            width: 820,
            ..Self::default()
        };
        let mut address = None;
        let mut parent = None;
        let mut preview = false;
        let mut iter = args.into_iter();
        while let Some(argument) = iter.next() {
            match argument.as_str() {
                "--demo" if !launch.demo => launch.demo = true,
                "--preview" if !preview => preview = true,
                "--verify-engine" if !launch.verify => launch.verify = true,
                "--url" if address.is_none() => {
                    address = Some(iter.next().ok_or("Missing engine address.")?)
                }
                "--parent" if parent.is_none() => {
                    parent = Some(iter.next().ok_or("Missing parent process.")?)
                }
                "--capture-dir" if launch.capture.is_none() => {
                    launch.capture =
                        Some(PathBuf::from(iter.next().ok_or("Missing capture folder.")?))
                }
                "--result" if launch.result.is_none() => {
                    launch.result = Some(PathBuf::from(iter.next().ok_or("Missing result path.")?))
                }
                "--scroll-benchmark" if launch.benchmark.is_none() => {
                    launch.benchmark = Some(PathBuf::from(
                        iter.next().ok_or("Missing benchmark output path.")?,
                    ));
                }
                "--width" => {
                    launch.width = iter
                        .next()
                        .ok_or("Missing width.")?
                        .parse()
                        .map_err(|_| "Invalid width.")?
                }
                _ => return Err("Invalid launch parameters. Start NovaMix.exe.".into()),
            }
        }
        if !(680..=1400).contains(&launch.width) {
            return Err("Invalid preview size.".into());
        }
        if launch.demo {
            if address.is_some() || parent.is_some() || launch.verify {
                return Err("Demo cannot connect to an engine.".into());
            }
        } else {
            if launch.capture.is_some() || launch.benchmark.is_some() {
                return Err("Screenshots are only available with demo data.".into());
            }
            launch.connection = Some(Connection::parse(
                &address.ok_or("Start NovaMix.exe to open the panel.")?,
                &parent.ok_or("Missing engine process.")?,
            )?);
        }
        Ok(launch)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Tab {
    Mixer,
    Streams,
    Settings,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Language {
    Italian,
    English,
}
impl std::fmt::Display for Language {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(if *self == Self::Italian {
            "Italiano"
        } else {
            "English"
        })
    }
}
#[derive(Debug, Clone)]
enum Message {
    Tick,
    WindowOpened(window::Id),
    WindowConfigured(bool),
    BenchmarkTick(Instant),
    Tab(Tab),
    Action(&'static str, i32),
    Language(Language),
    Route(usize, Device),
    ColorInput(usize, String),
    ColorApply(usize),
    Diagnostics,
    Hide,
    Quit,
    Capture(window::Screenshot),
    Noop,
}

struct App {
    benchmark: Option<crate::scroll_bench::Benchmark>,
    state: State,
    worker: Option<Worker>,
    demo: bool,
    connected: bool,
    tab: Tab,
    revision: u64,
    error: Option<String>,
    toast: Option<(String, Instant)>,
    colors: [String; 3],
    color_dirty: [bool; 3],
    reporting: bool,
    capture: Option<PathBuf>,
    capture_step: usize,
    capture_at: Instant,
    capture_pending: bool,
}

pub fn run(launch: Launch) -> iced::Result {
    let width = launch.width;
    let app = App::new(launch)
        .map_err(|error| iced::Error::WindowCreationFailed(std::io::Error::other(error).into()))?;
    // Store state in an Option so the boot closure can transfer ownership once.
    let boot = std::sync::Mutex::new(Some(app));
    iced::application(
        move || boot.lock().unwrap().take().expect("single UI boot"),
        App::update,
        App::view,
    )
    .title(|app: &App| {
        if app.demo {
            "NovaMix — Preview"
        } else {
            "NovaMix"
        }
        .to_owned()
    })
    .theme(App::theme)
    .subscription(App::subscription)
    .default_font(Font::with_name("Segoe UI"))
    .window(window::Settings {
        size: Size::new(width as f32, 840.0),
        min_size: Some(Size::new(680.0, 600.0)),
        position: window::Position::Centered,
        icon: icon(),
        ..window::Settings::default()
    })
    .run()
}

impl App {
    fn subscription(&self) -> iced::Subscription<Message> {
        let mut subscriptions = vec![window::open_events().map(Message::WindowOpened)];
        if self.benchmark.is_some() {
            subscriptions
                .push(iced::time::every(Duration::from_millis(16)).map(Message::BenchmarkTick));
        }
        if let Some(worker) = &self.worker {
            subscriptions.push(worker.subscription().map(|()| Message::Tick));
        }
        if self.capture.is_some() || self.toast.is_some() {
            subscriptions
                .push(iced::time::every(Duration::from_millis(100)).map(|_| Message::Tick));
        }
        iced::Subscription::batch(subscriptions)
    }
    fn new(launch: Launch) -> Result<Self, String> {
        let worker = launch.connection.map(Worker::start).transpose()?;
        let state = if launch.demo {
            State::demo()
        } else {
            State::default()
        };
        let colors = [
            state.battery_low,
            state.battery_medium,
            state.battery_normal,
        ]
        .map(|value| format!("#{value:06X}"));
        Ok(Self {
            state,
            worker,
            demo: launch.demo,
            connected: launch.demo,
            tab: if launch.benchmark.is_some() {
                Tab::Settings
            } else {
                Tab::Mixer
            },
            benchmark: launch.benchmark.map(crate::scroll_bench::Benchmark::new),
            revision: 0,
            error: None,
            toast: None,
            colors,
            color_dirty: [false; 3],
            reporting: false,
            capture: launch.capture,
            capture_step: 0,
            capture_at: Instant::now() + Duration::from_secs(2),
            capture_pending: false,
        })
    }
    fn t(&self, it: &'static str, en: &'static str) -> &'static str {
        if self.state.language != 0 { en } else { it }
    }
    fn theme(&self) -> Theme {
        Theme::custom(
            "NovaMix",
            iced::theme::Palette {
                background: Color::from_rgb(0.065, 0.075, 0.083),
                text: Color::from_rgb(0.94, 0.95, 0.96),
                primary: self.accent(),
                success: Color::from_rgb(0.47, 0.89, 0.72),
                danger: Color::from_rgb(1.0, 0.56, 0.57),
                warning: Color::from_rgb8(255, 195, 109),
            },
        )
    }
    fn accent(&self) -> Color {
        match self.state.accent {
            1 => Color::from_rgb8(158, 203, 255),
            2 => Color::from_rgb8(201, 182, 255),
            _ => Color::from_rgb8(145, 229, 196),
        }
    }
    fn update(&mut self, message: Message) -> Task<Message> {
        match message {
            Message::WindowOpened(id) => {
                if let Some(bench) = &mut self.benchmark {
                    bench.start = Some(Instant::now());
                }
                return window::run(id, crate::platform::configure_window)
                    .map(Message::WindowConfigured);
            }
            Message::WindowConfigured(configured) => {
                if let Some(bench) = &mut self.benchmark {
                    bench.configured = configured;
                }
            }
            Message::BenchmarkTick(scheduled) => {
                if let Some(bench) = &mut self.benchmark {
                    bench.frame(Instant::now(), scheduled);
                    if let Some(start) = bench.start {
                        let elapsed = start.elapsed();
                        if elapsed >= Duration::from_secs(4) {
                            let _ = bench.finish();
                            return iced::exit();
                        }
                        let phase = elapsed.as_secs_f32() % 1.6 / 1.6;
                        let y = if phase < 0.5 {
                            phase * 1800.0
                        } else {
                            (1.0 - phase) * 1800.0
                        };
                        return operation::scroll_to(
                            "page",
                            scrollable::AbsoluteOffset { x: 0.0, y },
                        );
                    }
                }
            }
            Message::Tick => {
                if let Some(worker) = &self.worker {
                    for event in worker.drain() {
                        match event {
                            Event::ParentExited => return iced::exit(),
                            Event::Error(error) => {
                                self.connected = false;
                                self.error = Some(error);
                            }
                            Event::State(next, revision) if revision >= self.revision => {
                                self.connected = true;
                                // A successful poll must not erase an action error before the user reads it.
                                self.state = *next;
                                for (index, value) in [
                                    self.state.battery_low,
                                    self.state.battery_medium,
                                    self.state.battery_normal,
                                ]
                                .into_iter()
                                .enumerate()
                                {
                                    if !self.color_dirty[index] {
                                        self.colors[index] = format!("#{value:06X}");
                                    }
                                }
                            }
                            Event::Report(result) => {
                                self.reporting = false;
                                match result {
                                    Ok(true) => {
                                        self.toast = Some((
                                            self.t("Report salvato.", "Report saved.").into(),
                                            Instant::now(),
                                        ))
                                    }
                                    Ok(false) => (),
                                    Err(error) => self.error = Some(error),
                                }
                            }
                            _ => (),
                        }
                    }
                }
                if self
                    .toast
                    .as_ref()
                    .is_some_and(|(_, at)| at.elapsed() > Duration::from_secs(5))
                {
                    self.toast = None;
                }
                if self.capture.is_some()
                    && !self.capture_pending
                    && Instant::now() >= self.capture_at
                {
                    self.capture_pending = true;
                    return window::latest().then(|id| {
                        id.map_or(Task::done(Message::Noop), |id| {
                            window::screenshot(id).map(Message::Capture)
                        })
                    });
                }
            }
            Message::Tab(tab) => {
                self.tab = tab;
                self.error = None;
                return operation::scroll_to("page", scrollable::AbsoluteOffset { x: 0.0, y: 0.0 });
            }
            Message::Action(name, value) => self.action(name, value),
            Message::Language(value) => {
                self.action("language", i32::from(value == Language::English))
            }
            Message::Route(index, device) => self.action(
                ["route-pc", "route-chat", "route-out"][index],
                device.number,
            ),
            Message::ColorInput(index, value) => {
                if value.len() <= 7 {
                    self.colors[index] = value;
                    self.color_dirty[index] = true;
                }
            }
            Message::ColorApply(index) => {
                if let Some(value) = color_value(&self.colors[index]) {
                    self.action(COLOR_KEYS[index], value);
                    self.color_dirty[index] = false;
                } else {
                    self.error = Some(
                        self.t(
                            "Usa un colore esadecimale, per esempio #FF8F91.",
                            "Use a hex color, for example #FF8F91.",
                        )
                        .into(),
                    );
                }
            }
            Message::Diagnostics => {
                if let Some(worker) = &self.worker {
                    if !self.reporting {
                        self.reporting = true;
                        worker.report(self.state.language != 0);
                    }
                } else {
                    self.toast = Some((
                        self.t(
                            "Esportazione disponibile collegando il motore.",
                            "Export is available when connected to the engine.",
                        )
                        .into(),
                        Instant::now(),
                    ));
                }
            }
            Message::Hide => return iced::exit(),
            Message::Quit => {
                if self.demo {
                    return iced::exit();
                }
                self.action("quit", 1);
                // The parent watcher exits the UI when the engine has acknowledged Quit.
            }
            Message::Capture(screenshot) => {
                let names = [
                    "mixer-it",
                    "streams-it",
                    "settings-it",
                    "settings-battery-it",
                    "settings-en",
                    "mixer-en",
                ];
                if let Some(folder) = &self.capture {
                    if let Err(error) = save_screenshot(
                        folder,
                        &format!("{}.png", names[self.capture_step]),
                        &screenshot,
                    ) {
                        self.error = Some(error);
                        return iced::exit();
                    }
                }
                self.capture_step += 1;
                if self.capture_step >= names.len() {
                    return iced::exit();
                }
                match self.capture_step {
                    1 => self.tab = Tab::Streams,
                    2 => self.tab = Tab::Settings,
                    3 => {
                        self.capture_pending = false;
                        self.capture_at = Instant::now() + Duration::from_millis(500);
                        return operation::scroll_to(
                            "page",
                            scrollable::AbsoluteOffset { x: 0.0, y: 550.0 },
                        );
                    }
                    4 => {
                        self.state.language = 1;
                        self.capture_pending = false;
                        self.capture_at = Instant::now() + Duration::from_millis(500);
                        return operation::scroll_to(
                            "page",
                            scrollable::AbsoluteOffset { x: 0.0, y: 0.0 },
                        );
                    }
                    5 => self.tab = Tab::Mixer,
                    _ => (),
                }
                self.capture_pending = false;
                self.capture_at = Instant::now() + Duration::from_millis(500);
            }
            Message::Noop => (),
        }
        Task::none()
    }
    fn action(&mut self, name: &'static str, value: i32) {
        if !self.connected {
            return;
        }
        if name.starts_with("route-")
            && (self.state.enabled != 0 || self.state.running_requested != 0)
        {
            return;
        }
        if name == "physical" && self.state.physical < 0 {
            return;
        }
        if name == "oled" && self.state.mic < 0 {
            return;
        }
        if name == "batteryBands"
            && (value / 100 >= value % 100 || value / 100 < 0 || value % 100 > 99)
        {
            return;
        }
        let revision = self.revision + 1;
        if let Some(worker) = &self.worker {
            if !worker.send(name, value, revision) {
                self.error = Some(
                    self.t(
                        "Comandi in attesa. Riprova tra un momento.",
                        "Commands are pending. Try again in a moment.",
                    )
                    .into(),
                );
                return;
            }
        }
        self.revision = revision;
        self.error = None;
        self.state.apply(name, value);
        if self.demo {
            if name == "enabled" {
                self.state.enabled = value;
                self.state.audio = value;
            }
            if name == "physical" {
                self.state.physical = value;
                self.state.physical_pending = 0;
            }
        }
    }
    fn view(&self) -> Element<'_, Message> {
        let s = &self.state;
        let active = s.enabled != 0;
        let run_label = if s.running_requested != 0 {
            self.t("Ferma audio", "Stop audio")
        } else if s.route_ready == 0 {
            self.t("Configura flussi", "Configure streams")
        } else {
            self.t("Avvia audio", "Start audio")
        };
        let run_message = if s.route_ready == 0 && s.running_requested == 0 {
            Message::Tab(Tab::Streams)
        } else {
            Message::Action("enabled", i32::from(s.running_requested == 0))
        };
        let brand = row![
            container(text("≋").size(30).color(self.accent()))
                .width(42)
                .height(42)
                .center_x(42)
                .center_y(42)
                .style(card_style),
            column![
                text("NovaMix").size(26),
                text(self.t("Il tuo audio, in equilibrio.", "Your audio, balanced."))
                    .size(12)
                    .color(MUTED)
            ]
            .spacing(1)
            .width(Fill),
            button(text(run_label).size(13))
                .width(152)
                .padding([10, 17])
                .on_press_maybe(self.connected.then_some(run_message))
        ]
        .spacing(12)
        .align_y(Alignment::Center);
        let status = if !self.connected {
            self.t("Connessione al mixer…", "Connecting to mixer…")
        } else if active {
            self.t("Audio attivo", "Audio running")
        } else {
            self.t("In pausa", "Paused")
        };
        let device_line = row![
            text("●")
                .color(if s.hid != 0 { self.accent() } else { MUTED })
                .size(10),
            text(if s.device.is_empty() {
                "Arctis Nova Pro Wireless"
            } else {
                &s.device
            })
            .size(12)
            .color(MUTED),
            Space::new().width(Fill),
            text(status)
                .size(12)
                .color(if active { self.accent() } else { MUTED })
        ]
        .spacing(8)
        .align_y(Alignment::Center);
        let mut navigation = row![].spacing(6);
        for (tab, label) in [
            (Tab::Mixer, "Mixer"),
            (Tab::Streams, self.t("Flussi", "Streams")),
            (Tab::Settings, self.t("Preferenze", "Settings")),
        ] {
            let selected = self.tab == tab;
            navigation = navigation.push(
                button(text(label).size(14))
                    .padding([10, 16])
                    .style(move |theme, status| {
                        if selected {
                            button::primary(theme, status)
                        } else {
                            quiet_button(theme, status)
                        }
                    })
                    .on_press(Message::Tab(tab)),
            );
        }
        let mut content = column![brand, device_line, navigation, horizontal_rule(1)].spacing(14);
        if let Some(error) = &self.error {
            content = content.push(alert(error));
        }
        if !s.error.is_empty() {
            content = content.push(alert(&s.error));
        } else if active && !s.hid_error.is_empty() {
            content=content.push(alert(self.t("Il mixer funziona, ma il controllo USB richiede una verifica. Esporta la diagnostica dalle Preferenze.","The mixer is running, but USB control needs attention. Export diagnostics from Settings.")));
        }
        let panel = match self.tab {
            Tab::Mixer => self.mixer(),
            Tab::Streams => self.streams(),
            Tab::Settings => self.settings(),
        };
        content = content.push(panel);
        let footer = row![
            text("0.21 · Native preview").size(11).color(MUTED),
            Space::new().width(Fill),
            button(text(self.t("Continua in background", "Keep running in background")).size(12))
                .style(quiet_button)
                .padding([8, 10])
                .on_press(Message::Hide)
        ]
        .align_y(Alignment::Center);
        if let Some((toast, _)) = &self.toast {
            content = content.push(text(toast).size(12).color(self.accent()));
        }
        content = content.push(footer);
        let page = container(content)
            .padding([20, 28])
            .width(Fill)
            .max_width(940);
        container(crate::smooth_scroll::page(
            scrollable(container(page).center_x(Fill)).id("page"),
            self.tab as u8,
        ))
        .height(Fill)
        .width(Fill)
        .into()
    }
    fn mixer(&self) -> Element<'_, Message> {
        let s = &self.state;
        let channel = |label: &'static str, value: i32, chat: bool| {
            column![
                text(label).size(13).color(MUTED),
                text(format!("{value}%")).size(46).color(if chat {
                    self.accent()
                } else {
                    Color::WHITE
                })
            ]
            .spacing(4)
        };
        let balance = card(
            column![
                row![
                    text("ChatMix").size(19),
                    Space::new().width(Fill),
                    button(text(self.t("Centra", "Center")).size(12))
                        .style(quiet_button)
                        .padding([7, 12])
                        .on_press_maybe(self.connected.then_some(Message::Action("balance", 0)))
                ]
                .align_y(Alignment::Center),
                row![
                    channel("PC", s.game, false),
                    Space::new().width(Fill),
                    channel("Chat", s.chat, true)
                ],
                slider(-100..=100, s.balance, |value| Message::Action(
                    "balance", value
                ))
                .step(1)
                .height(26),
                row![
                    text(self.t("Più PC", "More PC")).size(11).color(MUTED),
                    Space::new().width(Fill),
                    text(self.t("Centro", "Center")).size(11).color(MUTED),
                    Space::new().width(Fill),
                    text(self.t("Più Chat", "More chat")).size(11).color(MUTED)
                ],
                horizontal_rule(1),
                row![
                    text(self.t("Regolazione precisa", "Fine adjustment"))
                        .size(12)
                        .color(MUTED),
                    Space::new().width(Fill),
                    button(text("← PC  1%").size(12))
                        .style(quiet_button)
                        .padding([7, 12])
                        .on_press_maybe(
                            self.connected
                                .then_some(Message::Action("balance", (s.balance - 1).max(-100)))
                        ),
                    button(text("1%  Chat →").size(12))
                        .style(quiet_button)
                        .padding([7, 12])
                        .on_press_maybe(
                            self.connected
                                .then_some(Message::Action("balance", (s.balance + 1).min(100)))
                        )
                ]
                .spacing(8)
                .align_y(Alignment::Center)
            ]
            .spacing(11),
        );
        let mut volumes = column![text(self.t("Volumi", "Volume controls")).size(17)].spacing(10);
        if s.physical >= 0 {
            volumes = volumes.push(self.number_slider(
                self.t("Volume cuffie", "Headset volume"),
                "physical",
                s.physical_display.clamp(0, 100),
                0,
                100,
                true,
            ));
            if s.physical_pending != 0 {
                volumes = volumes.push(
                    text(self.t("In attesa della base…", "Waiting for the base…"))
                        .size(11)
                        .color(MUTED),
                );
            }
        } else {
            volumes = volumes.push(
                text(self.t(
                    "Volume cuffie: in attesa della base.",
                    "Headset volume: waiting for the base.",
                ))
                .size(13)
                .color(MUTED),
            );
        }
        volumes = volumes.push(self.number_slider(
            self.t("Volume del mix", "Mix volume"),
            "master",
            s.master,
            0,
            100,
            true,
        ));
        let mic = match s.mic {
            0 => self.t("Microfono attivo", "Microphone on"),
            1 => self.t("Microfono muto", "Microphone muted"),
            _ => self.t("Microfono non rilevato", "Microphone unavailable"),
        };
        let mic_color = if s.mic == 1 {
            Color::from_rgb8(255, 143, 145)
        } else {
            self.accent()
        };
        let hardware = card(
            row![
                column![
                    text(self.t("Microfono", "Microphone"))
                        .size(12)
                        .color(MUTED),
                    text(mic).size(14).color(mic_color)
                ]
                .spacing(8)
                .width(Fill),
                self.battery(
                    if s.charging == 2 {
                        self.t("Cuffie · in carica", "Headset · charging")
                    } else {
                        self.t("Batteria cuffie", "Headset battery")
                    },
                    s.battery
                ),
                self.battery(
                    self.t("Batteria nello slot", "Battery in charger"),
                    s.charger_battery
                )
            ]
            .spacing(14)
            .align_y(Alignment::Center),
        );
        column![
            balance,
            card(volumes),
            hardware,
            text(self.t(
                "La rotella controlla il mixer anche quando chiudi questa finestra.",
                "The headset wheel keeps working when you close this window."
            ))
            .size(12)
            .color(MUTED)
        ]
        .spacing(17)
        .into()
    }
    fn battery<'a>(&self, label: &'a str, value: i32) -> Element<'a, Message> {
        column![
            text(label).size(12).color(MUTED),
            text(if value < 0 {
                "—".into()
            } else {
                format!("{value}%")
            })
            .size(23)
        ]
        .spacing(5)
        .width(Fill)
        .into()
    }
    fn streams(&self) -> Element<'_, Message> {
        let s = &self.state;
        let locked = !self.connected || s.enabled != 0 || s.running_requested != 0;
        let labels = [
            self.t("Flusso PC", "PC stream"),
            self.t("Flusso Chat", "Chat stream"),
            self.t("Riproduci su", "Playback output"),
        ];
        let help = [
            self.t(
                "Imposta questa uscita come predefinita in Windows.",
                "Set this output as the Windows default.",
            ),
            self.t(
                "Assegna questa uscita a Discord, WhatsApp, Telegram e alle altre app di chat.",
                "Assign this output to Discord, WhatsApp, Telegram and other chat apps.",
            ),
            self.t(
                "Scegli le cuffie o la cassa Bluetooth su cui ascoltare.",
                "Choose the headset or Bluetooth speaker you want to hear.",
            ),
        ];
        let mut routes = column![].spacing(22);
        for index in 0..3 {
            let selected = s
                .devices
                .iter()
                .find(|device| device.number == s.routes[index])
                .cloned();
            let select = pick_list(s.devices.clone(), selected, move |device| {
                if locked {
                    Message::Noop
                } else {
                    Message::Route(index, device)
                }
            })
            .placeholder(self.t("Seleziona un'uscita", "Select an output"))
            .padding(12)
            .width(Fill)
            .text_size(14);
            let mut field = column![
                text(labels[index]).size(15),
                select,
                text(help[index]).size(12).color(MUTED)
            ]
            .spacing(9);
            if index < 2 {
                field = field
                    .push(progress_bar(0.0..=100.0, s.signal[index].clamp(0, 100) as f32).girth(4));
            }
            routes = routes.push(field);
        }
        column![text(self.t("Percorso audio","Audio routing")).size(22),
            text(self.t("Due flussi separati. Un'unica uscita per ascoltare.","Two separate streams. One playback output.")).size(13).color(MUTED),card(routes),
            text(if locked {self.t("Ferma l'audio prima di cambiare le uscite.","Stop audio before changing outputs.")}else{self.t("Scegli tre uscite distinte, poi premi Avvia audio.","Choose three different outputs, then select Start audio.")}).size(13).color(self.accent()),
            text(self.t("Per il flusso PC usa un'uscita digitale libera o un secondo dispositivo virtuale. Per Chat usa NovaMix Chat / CABLE Input. Il mixer rispetta i volumi delle singole app in Windows.","Use an unused digital output or a second virtual device for PC. For Chat, use NovaMix Chat / CABLE Input. The mixer preserves individual Windows app volumes.")).size(12).color(MUTED),
            text(self.t("Per tornare all'ascolto diretto, seleziona le cuffie in Windows e nelle app assegnate alla Chat.","To return to direct playback, select the headset in Windows and in apps assigned to Chat.")).size(12).color(MUTED)
        ].spacing(17).into()
    }
    fn settings(&self) -> Element<'_, Message> {
        let s = &self.state;
        let language = if s.language == 0 {
            Language::Italian
        } else {
            Language::English
        };
        let language_row = row![
            text("Lingua / Language").size(14),
            Space::new().width(Fill),
            pick_list(
                [Language::Italian, Language::English],
                Some(language),
                Message::Language
            )
            .text_size(13)
            .padding(10)
        ]
        .align_y(Alignment::Center);
        let mut accents = row![
            text(self.t("Colore d'accento", "Accent color")).size(14),
            Space::new().width(Fill)
        ]
        .spacing(8)
        .align_y(Alignment::Center);
        for (index, name) in [
            self.t("Menta", "Mint"),
            self.t("Blu", "Blue"),
            self.t("Viola", "Purple"),
        ]
        .into_iter()
        .enumerate()
        {
            let selected = s.accent == index as i32;
            accents = accents.push(
                button(text(name).size(12))
                    .padding([9, 13])
                    .style(move |theme, status| {
                        if selected {
                            button::primary(theme, status)
                        } else {
                            quiet_button(theme, status)
                        }
                    })
                    .on_press(Message::Action("accent", index as i32)),
            );
        }
        let controls = column![
            text(self.t("Controlli", "Controls")).size(17),
            self.toggle(
                self.t("Curva ChatMix più graduale", "Smoother ChatMix curve"),
                self.t(
                    "Passi più piccoli vicino allo zero e al centro.",
                    "Smaller steps near zero and the center."
                ),
                "gentleMix",
                s.gentle_mix,
                true
            ),
            self.toggle(
                self.t("Mostra overlay", "Show overlay"),
                self.t(
                    "ChatMix e volume vicino all'orologio.",
                    "ChatMix and volume changes next to the clock."
                ),
                "overlay",
                s.overlay,
                true
            ),
            self.toggle(
                self.t(
                    "Usa il volume della tastiera",
                    "Use keyboard volume controls"
                ),
                self.t(
                    "Sulle Nova regolano le cuffie. Sulle altre uscite torna il volume di Windows.",
                    "Controls headset volume on Nova routes. Other outputs keep normal Windows volume keys."
                ),
                "keyboard",
                s.keyboard,
                true
            ),
            self.toggle(
                self.t("Schermata NovaMix sulla base", "NovaMix base display"),
                self.t(
                    "ChatMix, volume, microfono e batterie sempre visibili.",
                    "Keep ChatMix, volume, microphone and batteries visible."
                ),
                "oledMix",
                s.oled_mix,
                true
            ),
            self.toggle(
                self.t(
                    "Avviso microfono sulla base",
                    "Microphone alert on the base"
                ),
                self.t(
                    "Avviso quando la schermata NovaMix è disattivata.",
                    "Show a mute alert when the NovaMix display is off."
                ),
                "oled",
                s.oled,
                s.mic >= 0
            ),
        ]
        .spacing(19);
        let tray=column![text(self.t("Barra delle applicazioni","System tray")).size(17),
            self.toggle(self.t("Mostra la percentuale","Show battery level as digits"),self.t("Carica delle cuffie vicino all'orologio.","Headset charge next to the Windows clock."),"batteryPercent",s.battery_percent,true),
            self.toggle(self.t("Sfondo della percentuale","Battery number background"),self.t("Cifre bianche su sfondo colorato; senza sfondo il colore si applica alle cifre.","White digits on a colored background; without it the digits use the battery color."),"batteryBackground",s.battery_background,s.battery_percent!=0),
            self.number_slider(self.t("Dimensione cifre (px)","Number size (px)"),"batteryFontSize",s.battery_font_size,8,16,s.battery_percent!=0),
            self.number_slider(self.t("Dimensione sfondo (px)","Background size (px)"),"batteryBackgroundSize",s.battery_background_size,8,16,s.battery_percent!=0&&s.battery_background!=0),
            self.colors_view(),
            self.band_slider(false),self.band_slider(true),
            self.toggle(self.t("Avviso batteria","Battery alert"),self.t("Una notifica quando la carica raggiunge la soglia.","Notify when charge reaches the selected threshold."),"batteryNotify",s.battery_notify,true),
            self.number_slider(self.t("Soglia di avviso (%)","Alert threshold (%)"),"batteryThreshold",s.battery_threshold,1,99,s.battery_notify!=0)
        ].spacing(21);
        let utility = row![
            column![
                text(self.t("Diagnostica", "Diagnostics")).size(15),
                text(self.t(
                    "Stato del mixer ed eventi USB.",
                    "Mixer status and USB events."
                ))
                .size(12)
                .color(MUTED)
            ]
            .spacing(4),
            Space::new().width(Fill),
            button(
                text(if self.reporting {
                    self.t("Esportazione…", "Exporting…")
                } else {
                    self.t("Esporta report", "Export report")
                })
                .size(12)
            )
            .style(quiet_button)
            .padding([10, 14])
            .on_press_maybe((!self.reporting && self.connected).then_some(Message::Diagnostics))
        ]
        .align_y(Alignment::Center);
        column![
            text(self.t("Fallo tuo.", "Make it yours.")).size(22),
            card(language_row),
            card(accents),
            card(controls),
            card(tray),
            card(utility),
            row![
                text(self.t(
                    "Termina la riproduzione dei due flussi.",
                    "Stop playback of both streams."
                ))
                .size(12)
                .color(MUTED),
                Space::new().width(Fill),
                button(text(self.t("Chiudi NovaMix", "Quit NovaMix")).size(12))
                    .style(button::danger)
                    .padding([9, 14])
                    .on_press(Message::Quit)
            ]
            .align_y(Alignment::Center)
        ]
        .spacing(16)
        .into()
    }
    fn colors_view(&self) -> Element<'_, Message> {
        let mut colors =
            column![text(self.t("Colori batteria", "Battery colors")).size(14)].spacing(10);
        let labels = [
            self.t("Bassa", "Low"),
            self.t("Media", "Medium"),
            self.t("Normale", "Normal"),
        ];
        for (index, label) in labels.into_iter().enumerate() {
            let value = color_value(&self.colors[index]).unwrap_or(0x929b9f);
            let swatch = Color::from_rgb8((value >> 16) as u8, (value >> 8) as u8, value as u8);
            colors = colors.push(
                row![
                    container(Space::new())
                        .width(20)
                        .height(20)
                        .style(move |_| container::Style {
                            background: Some(swatch.into()),
                            border: Border {
                                radius: 5.into(),
                                ..Border::default()
                            },
                            ..container::Style::default()
                        }),
                    text(label).size(13).width(Fill),
                    text_input("#RRGGBB", &self.colors[index])
                        .on_input(move |value| Message::ColorInput(index, value))
                        .on_submit(Message::ColorApply(index))
                        .width(115)
                        .size(13)
                        .padding(8),
                    button(text(self.t("Applica", "Apply")).size(12))
                        .style(quiet_button)
                        .padding([8, 12])
                        .on_press_maybe(self.connected.then_some(Message::ColorApply(index)))
                ]
                .spacing(10)
                .align_y(Alignment::Center),
            );
        }
        colors.into()
    }
    fn band_slider(&self, low: bool) -> Element<'_, Message> {
        let s = &self.state;
        let label = if low {
            self.t("Da media a bassa (%)", "Medium to low (%)")
        } else {
            self.t("Da normale a media (%)", "Normal to medium (%)")
        };
        let current = if low {
            s.battery_low_limit
        } else {
            s.battery_medium_limit
        };
        let range = if low {
            0..=(s.battery_medium_limit - 1).max(0)
        } else {
            (s.battery_low_limit + 1).min(99)..=99
        };
        let lo = s.battery_low_limit;
        let med = s.battery_medium_limit;
        column![
            row![
                text(label).size(13),
                Space::new().width(Fill),
                text(current).size(13).color(self.accent())
            ],
            slider(range, current, move |value| Message::Action(
                "batteryBands",
                if low {
                    value * 100 + med
                } else {
                    lo * 100 + value
                }
            ))
            .step(1)
            .height(20)
        ]
        .spacing(10)
        .into()
    }
    fn number_slider<'a>(
        &self,
        label: &'a str,
        name: &'static str,
        value: i32,
        min: i32,
        max: i32,
        enabled: bool,
    ) -> Element<'a, Message> {
        let available = enabled && self.connected;
        let heading = row![
            text(label)
                .size(13)
                .color(if available { Color::WHITE } else { MUTED }),
            Space::new().width(Fill),
            text(value)
                .size(14)
                .color(if available { self.accent() } else { MUTED })
        ];
        let mut control = column![heading].spacing(8);
        if available {
            control = control.push(
                slider(min..=max, value, move |value| Message::Action(name, value))
                    .step(1)
                    .height(18),
            );
        } else {
            control = control.push(progress_bar(min as f32..=max as f32, value as f32).girth(3));
        }
        control.into()
    }
    fn toggle<'a>(
        &self,
        label: &'a str,
        help: &'a str,
        name: &'static str,
        value: i32,
        enabled: bool,
    ) -> Element<'a, Message> {
        let enabled = enabled && self.connected;
        row![
            column![text(label).size(13), text(help).size(11).color(MUTED)]
                .spacing(5)
                .width(Fill),
            toggler(value != 0).size(21).on_toggle_maybe(
                enabled.then_some(move |flag| Message::Action(name, i32::from(flag)))
            )
        ]
        .spacing(22)
        .align_y(Alignment::Center)
        .into()
    }
}

fn card<'a>(content: impl Into<Element<'a, Message>>) -> Element<'a, Message> {
    container(content)
        .width(Fill)
        .padding(21)
        .style(card_style)
        .into()
}
fn card_style(_: &Theme) -> container::Style {
    container::Style {
        background: Some(SURFACE.into()),
        border: Border {
            color: LINE,
            width: 1.0,
            radius: 14.into(),
        },
        ..container::Style::default()
    }
}
fn quiet_button(theme: &Theme, status: button::Status) -> button::Style {
    let mut style = button::secondary(theme, status);
    style.background = Some(
        if matches!(status, button::Status::Hovered | button::Status::Pressed) {
            Color::from_rgb8(43, 49, 54)
        } else {
            Color::from_rgb8(32, 37, 41)
        }
        .into(),
    );
    style.border = Border {
        color: LINE,
        width: 1.0,
        radius: 8.into(),
    };
    style.text_color = if status == button::Status::Disabled {
        MUTED
    } else {
        theme.palette().text
    };
    style
}
fn alert<'a>(message: &'a str) -> Element<'a, Message> {
    container(
        text(message)
            .size(12)
            .color(Color::from_rgb8(255, 181, 173)),
    )
    .width(Fill)
    .padding(12)
    .style(|_| container::Style {
        background: Some(Color::from_rgb8(53, 34, 36).into()),
        border: Border {
            radius: 8.into(),
            ..Border::default()
        },
        ..container::Style::default()
    })
    .into()
}
fn color_value(text: &str) -> Option<i32> {
    let hex = text.strip_prefix('#')?;
    if hex.len() != 6 || !hex.bytes().all(|b| b.is_ascii_hexdigit()) {
        return None;
    }
    i32::from_str_radix(hex, 16).ok()
}
fn icon() -> Option<window::Icon> {
    let mut rgba = vec![0u8; 32 * 32 * 4];
    for y in 0..32usize {
        for x in 0..32usize {
            let inside = (4..28).contains(&x)
                || (4..28).contains(&y)
                || ((x as i32 - 16).pow(2) + (y as i32 - 16).pow(2) < 220);
            if !inside {
                continue;
            }
            let line = ((x == 9 || x == 10) && (8..25).contains(&y))
                || ((x == 16 || x == 17) && (6..27).contains(&y))
                || ((x == 23 || x == 24) && (8..25).contains(&y));
            let knob = ((7..13).contains(&x) && (12..16).contains(&y))
                || ((14..20).contains(&x) && (19..23).contains(&y))
                || ((21..27).contains(&x) && (9..13).contains(&y));
            let color = if line || knob {
                [145, 229, 196, 255]
            } else {
                [25, 40, 37, 255]
            };
            rgba[(y * 32 + x) * 4..(y * 32 + x + 1) * 4].copy_from_slice(&color);
        }
    }
    window::icon::from_rgba(rgba, 32, 32).ok()
}
fn save_screenshot(
    folder: &std::path::Path,
    name: &str,
    screen: &window::Screenshot,
) -> Result<(), String> {
    std::fs::create_dir_all(folder).map_err(|_| "Could not create preview folder.")?;
    let file = std::fs::File::create(folder.join(name)).map_err(|_| "Could not save preview.")?;
    let mut encoder = png::Encoder::new(
        std::io::BufWriter::new(file),
        screen.size.width,
        screen.size.height,
    );
    encoder.set_color(png::ColorType::Rgba);
    encoder.set_depth(png::BitDepth::Eight);
    encoder
        .write_header()
        .and_then(|mut writer| writer.write_image_data(&screen.rgba))
        .map_err(|_| "Could not encode preview.".into())
}

/// End-to-end IPC verification only ever mutates the engine's synthetic preview state.
pub fn verify_engine(connection: &Connection) -> Result<(), String> {
    let original = connection.state()?;
    if original.preview == 0 {
        return Err("Engine verification requires --preview. Live audio was not changed.".into());
    }
    let result = (|| {
        for (name, value) in [
            ("balance", -99),
            ("balance", 0),
            ("balance", 99),
            ("master", 37),
            ("physical", 42),
            ("language", 1),
            ("language", 0),
            ("accent", 2),
            ("gentleMix", 0),
            ("overlay", 0),
            ("keyboard", 1),
            ("oledMix", 0),
            ("oled", 1),
            ("batteryPercent", 1),
            ("batteryBackground", 0),
            ("batteryFontSize", 10),
            ("batteryBackgroundSize", 14),
            ("batteryNotify", 1),
            ("batteryThreshold", 30),
            ("batteryBands", 2040),
            ("batteryLow", 0x123456),
            ("batteryMedium", 0x654321),
            ("batteryNormal", 0xabcdef),
        ] {
            connection.action(name, value)?;
            let state = connection.state()?;
            let actual = match name {
                "balance" => state.balance,
                "master" => state.master,
                "physical" => state.physical,
                "language" => state.language,
                "accent" => state.accent,
                "gentleMix" => state.gentle_mix,
                "overlay" => state.overlay,
                "keyboard" => state.keyboard,
                "oledMix" => state.oled_mix,
                "oled" => state.oled,
                "batteryPercent" => state.battery_percent,
                "batteryBackground" => state.battery_background,
                "batteryFontSize" => state.battery_font_size,
                "batteryBackgroundSize" => state.battery_background_size,
                "batteryNotify" => state.battery_notify,
                "batteryThreshold" => state.battery_threshold,
                "batteryBands" => state.battery_low_limit * 100 + state.battery_medium_limit,
                "batteryLow" => state.battery_low,
                "batteryMedium" => state.battery_medium,
                "batteryNormal" => state.battery_normal,
                _ => -1,
            };
            if actual != value {
                return Err(format!("Engine did not apply {name}."));
            }
        }
        connection.action("enabled", 0)?;
        for (name, value) in [("route-pc", 2), ("route-chat", 1), ("route-out", 3)] {
            connection.action(name, value)?;
        }
        if connection.state()?.routes != [2, 1, 3] {
            return Err("Route selection failed.".into());
        }
        connection.action("enabled", 1)?;
        if connection.action("route-pc", 1).is_ok() {
            return Err("Active route was not protected.".into());
        }
        if !connection.diagnostics()?.starts_with("NovaMix") {
            return Err("Diagnostics unavailable.".into());
        }
        Ok(())
    })();
    // Restore the synthetic preview after verification, including failure cases.
    let _ = connection.action("enabled", 0);
    for (name, value) in [
        ("route-pc", original.routes[0]),
        ("route-chat", original.routes[1]),
        ("route-out", original.routes[2]),
        ("balance", original.balance),
        ("master", original.master),
        ("physical", original.physical_display),
        ("enabled", original.running_requested),
    ] {
        let _ = connection.action(name, value);
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn demo_never_accepts_a_live_engine_or_captures_real_state() {
        assert!(Launch::parse(vec!["--demo".into()]).is_ok());
        assert!(Launch::parse(vec!["--demo".into(), "--parent".into(), "1234".into()]).is_err());
        assert!(Launch::parse(vec!["--capture-dir".into(), "preview".into()]).is_err());
        assert!(Launch::parse(vec![]).is_err());
    }
    #[test]
    fn color_inputs_require_exact_hex_rgb() {
        assert_eq!(color_value("#ff8f91"), Some(0xff8f91));
        for value in ["red", "#fff", "#1234567", "#GG0000", "#123456\n"] {
            assert_eq!(color_value(value), None);
        }
    }
}
