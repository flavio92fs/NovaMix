//! Animate wheel notches inside the widget, without rebuilding the application
//! view on every frame. Pixel/touchpad scrolling and scrollbar dragging stay direct.
use iced::{
    Element, Event, Length, Rectangle, Renderer, Size, Theme, Vector,
    advanced::{
        Clipboard, Layout, Shell, Widget, layout, mouse, overlay, renderer,
        widget::{
            Id, Operation, Tree,
            operation::scrollable::{AbsoluteOffset, Scrollable},
            tree,
        },
    },
    window,
};
use std::time::Instant;

#[derive(Default)]
struct Motion {
    target: Option<f32>,
    last: Option<Instant>,
    page: u8,
}
impl Motion {
    fn cancel(&mut self) {
        self.target = None;
        self.last = None;
    }
    fn wheel(&mut self, position: f32, delta: f32, maximum: f32) {
        self.target = Some((self.target.unwrap_or(position) + delta).clamp(0.0, maximum));
        self.last.get_or_insert_with(Instant::now);
    }
    fn step(&mut self, position: f32, maximum: f32, now: Instant) -> f32 {
        let Some(target) = self.target.map(|target| target.clamp(0.0, maximum)) else {
            return position;
        };
        let dt = now
            .saturating_duration_since(self.last.unwrap_or(now))
            .as_secs_f32();
        self.last = Some(now);
        let position = target + (position - target) * (-dt / 0.035).exp();
        if (position - target).abs() < 0.2 {
            self.cancel();
            target
        } else {
            position
        }
    }
}

#[derive(Default)]
struct Metrics {
    offset: f32,
    maximum: f32,
}
impl Operation for Metrics {
    fn traverse(&mut self, _operate: &mut dyn FnMut(&mut dyn Operation)) {}
    fn scrollable(
        &mut self,
        _id: Option<&Id>,
        bounds: Rectangle,
        content: Rectangle,
        translation: Vector,
        _state: &mut dyn Scrollable,
    ) {
        self.offset = translation.y;
        self.maximum = (content.height - bounds.height).max(0.0);
    }
}
struct Position(f32);
impl Operation for Position {
    fn traverse(&mut self, _operate: &mut dyn FnMut(&mut dyn Operation)) {}
    fn scrollable(
        &mut self,
        _id: Option<&Id>,
        _bounds: Rectangle,
        _content: Rectangle,
        _translation: Vector,
        state: &mut dyn Scrollable,
    ) {
        state.scroll_to(AbsoluteOffset {
            x: None,
            y: Some(self.0),
        });
    }
}

struct SmoothPage<'a, Message> {
    content: Element<'a, Message>,
    page: u8,
}
pub fn page<'a, Message: 'a>(
    content: impl Into<Element<'a, Message>>,
    page: u8,
) -> Element<'a, Message> {
    Element::new(SmoothPage {
        content: content.into(),
        page,
    })
}
impl<Message> Widget<Message, Theme, Renderer> for SmoothPage<'_, Message> {
    fn tag(&self) -> tree::Tag {
        tree::Tag::of::<Motion>()
    }
    fn state(&self) -> tree::State {
        tree::State::new(Motion {
            page: self.page,
            ..Motion::default()
        })
    }
    fn children(&self) -> Vec<Tree> {
        vec![Tree::new(&self.content)]
    }
    fn diff(&self, tree: &mut Tree) {
        tree.diff_children(std::slice::from_ref(&self.content));
        let motion = tree.state.downcast_mut::<Motion>();
        if motion.page != self.page {
            motion.cancel();
            motion.page = self.page;
        }
    }
    fn size(&self) -> Size<Length> {
        self.content.as_widget().size()
    }
    fn size_hint(&self) -> Size<Length> {
        self.content.as_widget().size_hint()
    }
    fn layout(
        &mut self,
        tree: &mut Tree,
        renderer: &Renderer,
        limits: &layout::Limits,
    ) -> layout::Node {
        self.content
            .as_widget_mut()
            .layout(&mut tree.children[0], renderer, limits)
    }
    fn operate(
        &mut self,
        tree: &mut Tree,
        layout: Layout<'_>,
        renderer: &Renderer,
        operation: &mut dyn Operation,
    ) {
        let mut before = Metrics::default();
        self.content
            .as_widget_mut()
            .operate(&mut tree.children[0], layout, renderer, &mut before);
        self.content
            .as_widget_mut()
            .operate(&mut tree.children[0], layout, renderer, operation);
        let mut after = Metrics::default();
        self.content
            .as_widget_mut()
            .operate(&mut tree.children[0], layout, renderer, &mut after);
        if (after.offset - before.offset).abs() > 0.01 {
            tree.state.downcast_mut::<Motion>().cancel();
        }
    }
    fn update(
        &mut self,
        tree: &mut Tree,
        event: &Event,
        layout: Layout<'_>,
        cursor: mouse::Cursor,
        renderer: &Renderer,
        clipboard: &mut dyn Clipboard,
        shell: &mut Shell<'_, Message>,
        viewport: &Rectangle,
    ) {
        let mut before = Metrics::default();
        self.content
            .as_widget_mut()
            .operate(&mut tree.children[0], layout, renderer, &mut before);
        let motion = tree.state.downcast_mut::<Motion>();
        if matches!(
            event,
            Event::Mouse(mouse::Event::ButtonPressed(_)) | Event::Keyboard(_) | Event::Touch(_)
        ) {
            motion.cancel();
        }
        // Let the existing scrollable decide whether this wheel event belongs to
        // the page. Children and popup overlays retain their normal event priority.
        self.content.as_widget_mut().update(
            &mut tree.children[0],
            event,
            layout,
            cursor,
            renderer,
            clipboard,
            shell,
            viewport,
        );
        match event {
            Event::Mouse(mouse::Event::WheelScrolled {
                delta: mouse::ScrollDelta::Lines { .. },
            }) => {
                let mut after = Metrics::default();
                self.content.as_widget_mut().operate(
                    &mut tree.children[0],
                    layout,
                    renderer,
                    &mut after,
                );
                let delta = after.offset - before.offset;
                if delta.abs() > 0.01 {
                    motion.wheel(before.offset, delta, after.maximum);
                    self.content.as_widget_mut().operate(
                        &mut tree.children[0],
                        layout,
                        renderer,
                        &mut Position(before.offset),
                    );
                    shell.request_redraw();
                }
            }
            Event::Mouse(mouse::Event::WheelScrolled {
                delta: mouse::ScrollDelta::Pixels { .. },
            }) => motion.cancel(),
            Event::Window(window::Event::RedrawRequested(now)) if motion.target.is_some() => {
                let position = motion.step(before.offset, before.maximum, *now);
                self.content.as_widget_mut().operate(
                    &mut tree.children[0],
                    layout,
                    renderer,
                    &mut Position(position),
                );
                if motion.target.is_some() {
                    shell.request_redraw();
                }
            }
            _ => (),
        }
    }
    fn draw(
        &self,
        tree: &Tree,
        renderer: &mut Renderer,
        theme: &Theme,
        style: &renderer::Style,
        layout: Layout<'_>,
        cursor: mouse::Cursor,
        viewport: &Rectangle,
    ) {
        self.content.as_widget().draw(
            &tree.children[0],
            renderer,
            theme,
            style,
            layout,
            cursor,
            viewport,
        );
    }
    fn mouse_interaction(
        &self,
        tree: &Tree,
        layout: Layout<'_>,
        cursor: mouse::Cursor,
        viewport: &Rectangle,
        renderer: &Renderer,
    ) -> mouse::Interaction {
        self.content.as_widget().mouse_interaction(
            &tree.children[0],
            layout,
            cursor,
            viewport,
            renderer,
        )
    }
    fn overlay<'b>(
        &'b mut self,
        tree: &'b mut Tree,
        layout: Layout<'b>,
        renderer: &Renderer,
        viewport: &Rectangle,
        translation: Vector,
    ) -> Option<overlay::Element<'b, Message, Theme, Renderer>> {
        self.content.as_widget_mut().overlay(
            &mut tree.children[0],
            layout,
            renderer,
            viewport,
            translation,
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;
    #[test]
    fn wheel_motion_is_bounded_frame_rate_independent_and_stops() {
        for hz in [60, 144, 240] {
            let start = Instant::now();
            let mut motion = Motion::default();
            motion.wheel(0.0, 60.0, 500.0);
            motion.last = Some(start);
            let mut value = 0.0;
            for frame in 1..=hz {
                let next = motion.step(
                    value,
                    500.0,
                    start + Duration::from_secs_f32(frame as f32 / hz as f32),
                );
                assert!(next >= value && next <= 60.0);
                value = next;
            }
            assert_eq!(value, 60.0);
            assert!(motion.target.is_none());
        }
    }
    #[test]
    fn rapid_input_does_not_overshoot_and_reverses_without_a_queue() {
        let mut motion = Motion::default();
        for _ in 0..100 {
            motion.wheel(480.0, 60.0, 500.0);
        }
        assert_eq!(motion.target, Some(500.0));
        motion.wheel(480.0, -60.0, 500.0);
        assert_eq!(motion.target, Some(440.0));
        motion.cancel();
        assert!(motion.target.is_none());
        motion.wheel(0.0, -60.0, 500.0);
        assert_eq!(motion.target, Some(0.0));
    }
}
