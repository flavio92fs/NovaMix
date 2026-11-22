//! Optional reproducible rendering check. It only runs with synthetic demo data.
use std::{
    path::PathBuf,
    time::{Duration, Instant},
};
pub struct Benchmark {
    path: PathBuf,
    pub start: Option<Instant>,
    last: Option<Instant>,
    intervals: Vec<f64>,
    delays: Vec<f64>,
    pub configured: bool,
}
impl Benchmark {
    pub fn new(path: PathBuf) -> Self {
        Self {
            path,
            start: None,
            last: None,
            intervals: vec![],
            delays: vec![],
            configured: false,
        }
    }
    pub fn frame(&mut self, now: Instant, scheduled: Instant) {
        if self
            .start
            .is_some_and(|start| now.saturating_duration_since(start) > Duration::from_secs(1))
        {
            if let Some(last) = self.last {
                self.intervals
                    .push(now.saturating_duration_since(last).as_secs_f64() * 1000.0);
            }
            self.last = Some(now);
            self.delays
                .push(now.saturating_duration_since(scheduled).as_secs_f64() * 1000.0);
        }
    }
    pub fn finish(&self) -> std::io::Result<()> {
        let mut values = self.intervals.clone();
        values.sort_by(f64::total_cmp);
        let mut delays = self.delays.clone();
        delays.sort_by(f64::total_cmp);
        let percentile = |fraction: f64| {
            values
                .get(((values.len().saturating_sub(1)) as f64 * fraction).round() as usize)
                .copied()
                .unwrap_or(0.0)
        };
        let average = if values.is_empty() {
            0.0
        } else {
            values.iter().sum::<f64>() / values.len() as f64
        };
        let report = serde_json::json!({"demoOnly":true,"uiUpdates":values.len(),"averageUpdateIntervalMs":average,
            "medianUpdateIntervalMs":percentile(0.5),"p95UpdateIntervalMs":percentile(0.95),"p99UpdateIntervalMs":percentile(0.99),
            "p95UpdateDelayMs":delays.get(delays.len().saturating_sub(1)*95/100).copied().unwrap_or(0.0),
            "maximizeDisabled":self.configured,"requestedRenderer":std::env::var("ICED_BACKEND").unwrap_or("automatic".into())});
        std::fs::write(&self.path, serde_json::to_vec_pretty(&report)?)
    }
}
