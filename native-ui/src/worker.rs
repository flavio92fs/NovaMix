//! Audio and network calls never run on the UI thread. Slider writes are coalesced.
use crate::{
    platform::Parent,
    protocol::{Connection, State},
};
use iced::{Subscription, futures::channel::mpsc};
use std::{
    collections::VecDeque,
    sync::{Arc, Condvar, Mutex},
    time::{Duration, Instant},
};

#[derive(Debug, Clone)]
pub enum Event {
    State(Box<State>, u64),
    Error(String),
    ParentExited,
    Report(Result<bool, String>),
}
#[derive(Clone)]
struct Command {
    name: &'static str,
    value: i32,
    revision: u64,
}
#[derive(Default)]
struct Shared {
    commands: VecDeque<Command>,
    events: VecDeque<Event>,
    stop: bool,
    report: Option<bool>,
    wake: Option<mpsc::UnboundedSender<()>>,
}

#[derive(Clone)]
struct WakeSource(Arc<Mutex<Option<mpsc::UnboundedReceiver<()>>>>);
impl std::hash::Hash for WakeSource {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        std::hash::Hash::hash(&(Arc::as_ptr(&self.0) as usize), state);
    }
}

pub struct Worker {
    shared: Arc<(Mutex<Shared>, Condvar)>,
    source: WakeSource,
}
impl Worker {
    fn empty() -> Self {
        let (sender, receiver) = mpsc::unbounded();
        Self {
            shared: Arc::new((
                Mutex::new(Shared {
                    wake: Some(sender),
                    ..Shared::default()
                }),
                Condvar::new(),
            )),
            source: WakeSource(Arc::new(Mutex::new(Some(receiver)))),
        }
    }
    pub fn subscription(&self) -> Subscription<()> {
        Subscription::run_with(self.source.clone(), |source| {
            source
                .0
                .lock()
                .unwrap()
                .take()
                .expect("single IPC subscription")
        })
    }
    pub fn start(connection: Connection) -> Result<Self, String> {
        let parent = Parent::open(connection.parent)?;
        if !parent.alive() {
            return Err("NovaMix engine has exited.".into());
        }
        let worker = Self::empty();
        let background = worker.shared.clone();
        std::thread::Builder::new()
            .name("NovaMix IPC".into())
            .spawn(move || {
                let mut revision = 0;
                let mut next_poll = Instant::now();
                let mut last_state: Option<(State, u64)> = None;
                let mut last_error: Option<String> = None;
                loop {
                    let (commands, report) = {
                        let mut state = background.0.lock().unwrap();
                        if state.stop {
                            break;
                        }
                        if state.commands.is_empty()
                            && state.report.is_none()
                            && Instant::now() < next_poll
                        {
                            let wait = next_poll.saturating_duration_since(Instant::now());
                            state = background.1.wait_timeout(state, wait).unwrap().0;
                            if state.stop {
                                break;
                            }
                        }
                        (
                            state.commands.drain(..).collect::<Vec<_>>(),
                            state.report.take(),
                        )
                    };
                    if !parent.alive() {
                        push(&background, Event::ParentExited);
                        break;
                    }
                    let modified = !commands.is_empty();
                    for command in commands {
                        revision = command.revision;
                        if let Err(error) = connection.action(command.name, command.value) {
                            push(&background, Event::Error(error));
                        }
                    }
                    if let Some(english) = report {
                        let text = connection.diagnostics();
                        let target = background.clone();
                        std::thread::spawn(move || {
                            let result =
                                text.and_then(|text| crate::platform::save_report(&text, english));
                            push(&target, Event::Report(result));
                        });
                    }
                    if modified || Instant::now() >= next_poll {
                        match connection.state() {
                            Ok(state) => {
                                if last_state.as_ref().is_none_or(|(previous, version)| {
                                    previous != &state || *version != revision
                                }) {
                                    last_state = Some((state.clone(), revision));
                                    push(&background, Event::State(Box::new(state), revision));
                                }
                                last_error = None;
                            }
                            Err(error) => {
                                last_state = None;
                                if last_error.as_ref() != Some(&error) {
                                    last_error = Some(error.clone());
                                    push(&background, Event::Error(error));
                                }
                            }
                        }
                        next_poll = Instant::now() + Duration::from_millis(350);
                    }
                }
            })
            .map_err(|_| "Could not start the NovaMix connection worker.".to_owned())?;
        Ok(worker)
    }
    pub fn send(&self, name: &'static str, value: i32, revision: u64) -> bool {
        let mut state = self.shared.0.lock().unwrap();
        if coalesced(name) {
            // Never move a new slider target ahead of a start/stop/route command.
            if let Some(last) = state.commands.back_mut().filter(|last| last.name == name) {
                *last = Command {
                    name,
                    value,
                    revision,
                };
                self.shared.1.notify_one();
                return true;
            }
        }
        if state.commands.len() >= 96 {
            return false;
        }
        state.commands.push_back(Command {
            name,
            value,
            revision,
        });
        self.shared.1.notify_one();
        true
    }
    pub fn report(&self, english: bool) {
        self.shared.0.lock().unwrap().report = Some(english);
        self.shared.1.notify_one();
    }
    pub fn drain(&self) -> Vec<Event> {
        self.shared.0.lock().unwrap().events.drain(..).collect()
    }
}
impl Drop for Worker {
    fn drop(&mut self) {
        self.shared.0.lock().unwrap().stop = true;
        self.shared.1.notify_one();
    }
}
fn coalesced(name: &str) -> bool {
    matches!(
        name,
        "balance"
            | "physical"
            | "master"
            | "batteryFontSize"
            | "batteryBackgroundSize"
            | "batteryThreshold"
            | "batteryBands"
    )
}
fn push(shared: &Arc<(Mutex<Shared>, Condvar)>, event: Event) {
    let mut state = shared.0.lock().unwrap();
    let notify = state.events.is_empty();
    if matches!(event, Event::State(..)) {
        state.events.retain(|old| !matches!(old, Event::State(..)));
    }
    if state.events.len() >= 16 {
        state.events.pop_front();
    }
    state.events.push_back(event);
    if notify {
        if let Some(wake) = &state.wake {
            let _ = wake.unbounded_send(());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn slider_queue_is_bounded_and_preserves_control_order() {
        let worker = Worker::empty();
        for revision in 1..=1000 {
            assert!(worker.send("balance", 50, revision));
        }
        assert_eq!(worker.shared.0.lock().unwrap().commands.len(), 1);
        worker.send("enabled", 0, 1001);
        worker.send("balance", 25, 1002);
        let state = worker.shared.0.lock().unwrap();
        assert_eq!(
            state.commands.iter().map(|c| c.name).collect::<Vec<_>>(),
            ["balance", "enabled", "balance"]
        );
        assert_eq!(state.commands.back().unwrap().revision, 1002);
    }
}
