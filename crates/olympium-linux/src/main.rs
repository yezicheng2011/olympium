// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

#![forbid(unsafe_code)]

use gtk4::prelude::*;
use gtk4::{
    gio, glib, Align, Box, Button, DropDown, Grid, Label, ListBox, Orientation, Paned,
    ScrolledWindow, SelectionMode, Stack, StackSidebar, TextView, WrapMode,
};
use libadwaita as adw;
use olympium_core::{
    bundled_lock, choose_mirror, doctor, execute, load_catalog, load_config, matrix, prepare,
    request_for_file, runtime_rows, save_catalog, save_config, write_mirror, Appearance, Error,
    Execution, HostPlatform, LaunchRequest, Layout, RunOptions, SystemNet, SystemProbe,
};
use std::cell::RefCell;
use std::rc::Rc;
use std::sync::mpsc::{self, Receiver, TryRecvError};
use std::time::Duration;

struct State {
    layout: Layout,
    config: olympium_core::Config,
    toast: adw::ToastOverlay,
    list: ListBox,
    ids: RefCell<Vec<String>>,
    selected: RefCell<Option<String>>,
    output: gtk4::TextBuffer,
    run_button: Button,
    app: adw::Application,
}

fn main() -> glib::ExitCode {
    let application = adw::Application::builder()
        .application_id("app.olympium.linux")
        .flags(gio::ApplicationFlags::NON_UNIQUE)
        .build();
    application.connect_activate(build_ui);
    application.run()
}

fn build_ui(app: &adw::Application) {
    let layout = Layout::from_env();
    let config = load_config(&layout.config).unwrap_or_default();
    apply_appearance(config.appearance);

    let toast = adw::ToastOverlay::new();
    let output = gtk4::TextBuffer::new(None::<&gtk4::TextTagTable>);
    let list = ListBox::new();
    list.set_selection_mode(SelectionMode::Single);
    list.add_css_class("boxed-list");
    let run_button = Button::with_label("Run");
    run_button.add_css_class("suggested-action");
    let state = Rc::new(State {
        layout,
        config,
        toast: toast.clone(),
        list: list.clone(),
        ids: RefCell::new(Vec::new()),
        selected: RefCell::new(None),
        output: output.clone(),
        run_button: run_button.clone(),
        app: app.clone(),
    });

    let header = adw::HeaderBar::new();
    if let Some(path) = app_icon_file() {
        let mark = gtk4::Image::from_file(path);
        mark.set_pixel_size(20);
        header.pack_start(&mark);
    }
    let title = Label::new(Some("Olympium"));
    title.add_css_class("title");
    header.set_title_widget(Some(&title));
    header.pack_end(&appearance_dropdown(&state));

    let stack = Stack::new();
    stack.add_titled(&overview_page(&state), Some("overview"), "Overview");
    stack.add_titled(
        &programs_page(&state, &run_button),
        Some("programs"),
        "Programs",
    );
    stack.add_titled(&engines_page(&state), Some("engines"), "Engines");
    stack.add_titled(&runtimes_page(&state), Some("runtimes"), "Runtimes");
    stack.add_titled(&matrix_page(), Some("compatibility"), "Compatibility");
    stack.add_titled(&limits_page(), Some("limits"), "Limits");
    let sidebar = StackSidebar::new();
    sidebar.set_stack(&stack);

    let split = Paned::new(Orientation::Horizontal);
    split.set_start_child(Some(&sidebar));
    split.set_end_child(Some(&stack));
    split.set_resize_start_child(false);
    split.set_shrink_start_child(false);
    split.set_position(220);

    toast.set_child(Some(&split));
    toast.set_vexpand(true);
    let root = Box::new(Orientation::Vertical, 0);
    root.append(&header);
    root.append(&toast);

    let window = adw::ApplicationWindow::builder()
        .application(app)
        .title("Olympium")
        .content(&root)
        .default_width(1080)
        .default_height(700)
        .build();
    let ready_state = Rc::clone(&state);
    window.connect_map(move |_| {
        if let Ok(path) = std::env::var("OLYMPIUM_SELF_CHECK_READY")
            .or_else(|_| std::env::var("LINKSPAN_SELF_CHECK_READY"))
        {
            let _ = std::fs::write(path, "ready");
        }
        let _ = &ready_state;
    });
    refresh_programs(&state);
    window.present();

    if let Ok(path) = std::env::var("OLYMPIUM_SELF_CHECK")
        .or_else(|_| std::env::var("LINKSPAN_SELF_CHECK"))
    {
        let state = Rc::clone(&state);
        match request_for_file(
            std::path::Path::new(&path),
            None,
            None,
            None,
            None,
            Vec::new(),
        ) {
            Ok(request) => {
                let finished = Rc::clone(&state);
                start_program(&state, request, move |result| {
                    let text = match &result {
                        Ok(execution) => execution.stdout.clone(),
                        Err(err) => err.message().to_string(),
                    };
                    if let Ok(out) = std::env::var("OLYMPIUM_SELF_CHECK_OUT")
                        .or_else(|_| std::env::var("LINKSPAN_SELF_CHECK_OUT"))
                    {
                        let _ = std::fs::write(out, text);
                    }
                    finished.app.quit();
                });
            }
            Err(err) => {
                if let Ok(out) = std::env::var("OLYMPIUM_SELF_CHECK_OUT")
                    .or_else(|_| std::env::var("LINKSPAN_SELF_CHECK_OUT"))
                {
                    let _ = std::fs::write(out, err.message());
                }
                state.app.quit();
            }
        }
    }
}

fn app_icon_file() -> Option<std::path::PathBuf> {
    let mut candidates = Vec::new();
    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            for relative in [
                "../share/icons/hicolor/48x48/apps/olympium.png",
                "../../share/icons/hicolor/48x48/apps/olympium.png",
                "../../../share/icons/hicolor/48x48/apps/olympium.png",
            ] {
                candidates.push(dir.join(relative));
            }
        }
    }
    candidates.push(std::path::PathBuf::from(
        "/usr/share/icons/hicolor/48x48/apps/olympium.png",
    ));
    candidates.into_iter().find(|path| path.is_file())
}

fn appearance_dropdown(state: &Rc<State>) -> DropDown {
    let dropdown = DropDown::from_strings(&["Follow system", "Light", "Dark"]);
    dropdown.set_selected(match state.config.appearance {
        Appearance::System => 0,
        Appearance::Light => 1,
        Appearance::Dark => 2,
    });
    let guard = Rc::new(RefCell::new(true));
    let state = Rc::clone(state);
    let guard_for_signal = Rc::clone(&guard);
    dropdown.connect_selected_notify(move |dropdown| {
        if *guard_for_signal.borrow() {
            return;
        }
        let appearance = match dropdown.selected() {
            1 => Appearance::Light,
            2 => Appearance::Dark,
            _ => Appearance::System,
        };
        apply_appearance(appearance);
        let mut config = state.config.clone();
        config.appearance = appearance;
        if let Err(err) = save_config(&state.layout.config, &config) {
            toast(&state, err.message());
        }
    });
    *guard.borrow_mut() = false;
    dropdown
}

fn apply_appearance(appearance: Appearance) {
    let scheme = match appearance {
        Appearance::System => adw::ColorScheme::Default,
        Appearance::Light => adw::ColorScheme::ForceLight,
        Appearance::Dark => adw::ColorScheme::ForceDark,
    };
    adw::StyleManager::default().set_color_scheme(scheme);
}

fn overview_page(state: &Rc<State>) -> ScrolledWindow {
    let box_ = Box::new(Orientation::Vertical, 12);
    box_.set_margin_top(24);
    box_.set_margin_bottom(24);
    box_.set_margin_start(24);
    box_.set_margin_end(24);
    let heading = Label::new(Some("Programs from other systems, on this Linux computer."));
    heading.set_wrap(true);
    heading.set_xalign(0.0);
    heading.add_css_class("title-1");
    let body = Label::new(Some(
        "Add an APK, a Windows executable, a Linux package, an OpenHarmony HAP, or a Mac or iOS file you have the right to use. Linux programs run here. Windows programs run with Wine, which opens a native window. OpenHarmony packages run in a real OpenHarmony runtime after its slim image is downloaded. Mac and iOS software stays in the library and is not started.",
    ));
    body.set_wrap(true);
    body.set_xalign(0.0);
    body.set_selectable(true);
    let host = Label::new(Some(&format!(
        "This computer is {}. The library is {}.",
        HostPlatform::current().label(),
        state.layout.catalog.display()
    )));
    host.set_wrap(true);
    host.set_xalign(0.0);
    box_.append(&heading);
    box_.append(&body);
    box_.append(&host);
    scroll(&box_)
}

fn programs_page(state: &Rc<State>, run_button: &Button) -> Box {
    let page = Box::new(Orientation::Vertical, 8);
    page.set_margin_top(12);
    page.set_margin_bottom(12);
    page.set_margin_start(12);
    page.set_margin_end(12);
    let actions = Box::new(Orientation::Horizontal, 8);
    let add = Button::with_label("Add program");
    let state_add = Rc::clone(state);
    add.connect_clicked(move |_| choose_file(&state_add));
    let state_run = Rc::clone(state);
    run_button.connect_clicked(move |_| run_selected(&state_run));
    actions.append(&add);
    actions.append(run_button);
    let list_scroll = scroll(&state.list);
    list_scroll.set_vexpand(true);
    list_scroll.set_min_content_height(180);
    let output_view = TextView::with_buffer(&state.output);
    output_view.set_editable(false);
    output_view.set_monospace(true);
    output_view.set_wrap_mode(WrapMode::WordChar);
    let output_scroll = scroll(&output_view);
    output_scroll.set_vexpand(true);
    page.append(&actions);
    page.append(&list_scroll);
    page.append(&output_scroll);
    let state_select = Rc::clone(state);
    state.list.connect_row_selected(move |_, row| {
        *state_select.selected.borrow_mut() =
            row.and_then(|row| state_select.ids.borrow().get(row.index() as usize).cloned());
    });
    page
}

fn engines_page(state: &State) -> ScrolledWindow {
    let box_ = Box::new(Orientation::Vertical, 8);
    box_.set_margin_top(16);
    box_.set_margin_bottom(16);
    box_.set_margin_start(16);
    box_.set_margin_end(16);
    let intro = Label::new(Some(
        "Slim Android, Linux, OpenHarmony, and Wine runtimes are downloaded when you ask, from their own repositories. They are not inside this app. Wine is kept for Windows programs and is not used for OpenHarmony.",
    ));
    intro.set_wrap(true);
    intro.set_xalign(0.0);
    box_.append(&intro);
    let report = doctor(
        HostPlatform::current(),
        &state.config,
        &SystemProbe,
        &state.layout,
    );
    for engine in report.engines.iter().filter(|engine| engine.applicable) {
        let row = Label::new(Some(&format!(
            "{} — {} — {}",
            engine.name, engine.state, engine.detail
        )));
        row.set_wrap(true);
        row.set_xalign(0.0);
        row.set_selectable(true);
        box_.append(&row);
    }
    scroll(&box_)
}

fn runtimes_page(state: &Rc<State>) -> ScrolledWindow {
    let box_ = Box::new(Orientation::Vertical, 8);
    box_.set_margin_top(16);
    box_.set_margin_bottom(16);
    box_.set_margin_start(16);
    box_.set_margin_end(16);
    let intro = Label::new(Some(
        "Slim images are downloaded from their repositories. They are not inside Olympium. GitHub is used unless the client IP is in mainland China, or you set mirrors.prefer.",
    ));
    intro.set_wrap(true);
    intro.set_xalign(0.0);
    let status = Label::new(Some("Looking up the client IP…"));
    status.set_wrap(true);
    status.set_xalign(0.0);
    status.set_selectable(true);
    box_.append(&intro);
    box_.append(&status);
    let rows = runtime_rows(&bundled_lock(), &state.layout);
    for row in rows {
        let label = Label::new(Some(&row.replace('\t', " — ")));
        label.set_wrap(true);
        label.set_xalign(0.0);
        box_.append(&label);
    }
    if std::env::var_os("OLYMPIUM_SELF_CHECK").is_some()
        || std::env::var_os("LINKSPAN_SELF_CHECK").is_some()
        || std::env::var_os("OLYMPIUM_SKIP_RUNTIME_FETCH").is_some()
        || std::env::var_os("LINKSPAN_SKIP_RUNTIME_FETCH").is_some()
    {
        status.set_text("Runtime lookup skipped.");
        return scroll(&box_);
    }
    let layout = state.layout.clone();
    let mirrors = state.config.engines.mirrors.clone();
    let (tx, rx) = mpsc::channel();
    std::thread::spawn(move || {
        let net = SystemNet;
        let message = match choose_mirror(&mirrors, &net) {
            Ok(choice) => {
                let _ = write_mirror(&layout, &choice);
                let source = match choice.source {
                    olympium_core::MirrorSource::Github => "GitHub",
                    olympium_core::MirrorSource::China => "China mirror",
                };
                let ip = choice.client_ip.unwrap_or_else(|| "not reported".into());
                format!("{source}. {ip}. {}", choice.reason)
            }
            Err(err) => err.message().to_string(),
        };
        let _ = tx.send(message);
    });
    let rx = Rc::new(RefCell::new(Some(rx)));
    glib::timeout_add_local(Duration::from_millis(100), move || {
        let message = {
            let slot = rx.borrow();
            match slot.as_ref().map(Receiver::try_recv) {
                Some(Ok(text)) => Some(text),
                Some(Err(TryRecvError::Disconnected)) => {
                    Some("The lookup stopped before it finished.".into())
                }
                _ => None,
            }
        };
        if let Some(message) = message {
            status.set_text(&format!("Download source: {message}"));
            glib::ControlFlow::Break
        } else {
            glib::ControlFlow::Continue
        }
    });
    scroll(&box_)
}

fn matrix_page() -> ScrolledWindow {
    let grid = Grid::new();
    grid.set_column_spacing(16);
    grid.set_row_spacing(8);
    grid.set_margin_top(16);
    grid.set_margin_bottom(16);
    grid.set_margin_start(16);
    grid.set_margin_end(16);
    let corner = Label::new(Some("Host"));
    corner.add_css_class("heading");
    grid.attach(&corner, 0, 0, 1, 1);
    for (column, guest) in olympium_core::GuestSystem::ALL.into_iter().enumerate() {
        let label = Label::new(Some(guest.label()));
        label.add_css_class("heading");
        grid.attach(&label, (column as i32) + 1, 0, 1, 1);
    }
    for (row, cell_row) in matrix().iter().enumerate() {
        if cell_row.guest == olympium_core::GuestSystem::Android {
            let host = Label::new(Some(cell_row.host.label()));
            host.set_xalign(0.0);
            grid.attach(&host, 0, (row as i32) + 1, 1, 1);
        }
    }
    for (index, cell) in matrix().iter().enumerate() {
        let host_index = olympium_core::HostPlatform::ALL
            .iter()
            .position(|host| *host == cell.host)
            .unwrap_or(0);
        let guest_index = olympium_core::GuestSystem::ALL
            .iter()
            .position(|guest| *guest == cell.guest)
            .unwrap_or(0);
        let label = Label::new(Some(cell.level.title()));
        label.set_tooltip_text(Some(&format!("{}\n{}", cell.summary, cell.legal_note)));
        label.set_xalign(0.0);
        grid.attach(
            &label,
            (guest_index as i32) + 1,
            (host_index as i32) + 1,
            1,
            1,
        );
        let _ = index;
    }
    scroll(&grid)
}

fn limits_page() -> ScrolledWindow {
    let box_ = Box::new(Orientation::Vertical, 16);
    box_.set_margin_top(16);
    box_.set_margin_bottom(16);
    box_.set_margin_start(16);
    box_.set_margin_end(16);
    for limit in olympium_core::limits() {
        let title = Label::new(Some(limit.title));
        title.set_xalign(0.0);
        title.add_css_class("title-2");
        let body = Label::new(Some(limit.body));
        body.set_wrap(true);
        body.set_xalign(0.0);
        body.set_selectable(true);
        box_.append(&title);
        box_.append(&body);
    }
    scroll(&box_)
}

fn choose_file(state: &Rc<State>) {
    let dialog = gtk4::FileDialog::new();
    dialog.set_title("Add a program");
    let state = Rc::clone(state);
    dialog.open(
        state.app.active_window().as_ref(),
        None::<&gio::Cancellable>,
        move |result| {
            if let Ok(file) = result {
                if let Some(path) = file.path() {
                    match request_for_file(&path, None, None, None, None, Vec::new()) {
                        Ok(request) => add_request(&state, request),
                        Err(err) => toast(&state, err.message()),
                    }
                }
            }
        },
    );
}

fn add_request(state: &Rc<State>, request: LaunchRequest) {
    let mut catalog = match load_catalog(&state.layout.catalog) {
        Ok(catalog) => catalog,
        Err(err) => {
            toast(state, err.message());
            return;
        }
    };
    let name = request.name.clone();
    catalog.add(request);
    if let Err(err) = save_catalog(&state.layout.catalog, &catalog) {
        toast(state, err.message());
        return;
    }
    refresh_programs(state);
    toast(state, &format!("Added {name}."));
}

fn refresh_programs(state: &State) {
    while let Some(row) = state.list.row_at_index(0) {
        state.list.remove(&row);
    }
    let catalog = load_catalog(&state.layout.catalog).unwrap_or_default();
    let mut ids = Vec::new();
    if catalog.programs.is_empty() {
        let empty = Label::new(Some("No programs yet."));
        empty.set_margin_top(12);
        empty.set_margin_bottom(12);
        empty.set_margin_start(12);
        state.list.append(&empty);
    }
    for program in &catalog.programs {
        let label = Label::new(Some(&format!(
            "{} — {}",
            program.name,
            program.guest.label()
        )));
        label.set_xalign(0.0);
        label.set_margin_top(8);
        label.set_margin_bottom(8);
        label.set_margin_start(12);
        label.set_margin_end(12);
        state.list.append(&label);
        ids.push(program.id.clone());
    }
    *state.ids.borrow_mut() = ids;
}

fn run_selected(state: &Rc<State>) {
    let Some(id) = state.selected.borrow().clone() else {
        toast(state, "Select a program first.");
        return;
    };
    let catalog = match load_catalog(&state.layout.catalog) {
        Ok(catalog) => catalog,
        Err(err) => {
            toast(state, err.message());
            return;
        }
    };
    let Some(program) = catalog.get(&id) else {
        toast(state, "That program is no longer in the library.");
        return;
    };
    let request = program.to_request();
    let finished = Rc::clone(state);
    start_program(state, request, move |result| show_result(&finished, result));
}

fn start_program<F>(state: &Rc<State>, request: LaunchRequest, done: F)
where
    F: FnOnce(Result<Execution, Error>) + 'static,
{
    state.run_button.set_sensitive(false);
    let config = match load_config(&state.layout.config) {
        Ok(config) => config,
        Err(err) => {
            state.run_button.set_sensitive(true);
            done(Err(err));
            return;
        }
    };
    let host = HostPlatform::current();
    let layout = state.layout.clone();
    let (tx, rx) = mpsc::channel();
    std::thread::spawn(move || {
        let result = prepare(host, &request, &config, &SystemProbe).and_then(|prepared| {
            execute(
                &prepared,
                &RunOptions {
                    capture: true,
                    detach: false,
                },
                &layout,
            )
        });
        let _ = tx.send(result);
    });
    let rx = Rc::new(RefCell::new(Some(rx)));
    let state = Rc::clone(state);
    let done = Rc::new(RefCell::new(Some(done)));
    glib::timeout_add_local(Duration::from_millis(50), move || {
        let message = {
            let slot = rx.borrow();
            match slot.as_ref().map(Receiver::try_recv) {
                Some(Ok(result)) => Some(result),
                Some(Err(TryRecvError::Empty)) => None,
                Some(Err(TryRecvError::Disconnected)) | None => None,
            }
        };
        if let Some(result) = message {
            state.run_button.set_sensitive(true);
            if let Some(done) = done.borrow_mut().take() {
                done(result);
            }
            glib::ControlFlow::Break
        } else {
            glib::ControlFlow::Continue
        }
    });
}

fn show_result(state: &State, result: Result<Execution, Error>) {
    match result {
        Ok(execution) => {
            let mut text = format!("{}\n", execution.summary);
            if !execution.stdout.is_empty() {
                text.push_str(&execution.stdout);
            }
            if !execution.stderr.is_empty() {
                text.push_str(&execution.stderr);
            }
            state.output.set_text(&text);
            toast(state, "The program finished.");
        }
        Err(err) => {
            state.output.set_text(err.message());
            toast(state, err.message());
        }
    }
}

fn toast(state: &State, message: &str) {
    let toast = adw::Toast::new(message);
    toast.set_timeout(5);
    state.toast.add_toast(toast);
}

fn scroll(child: &impl gtk4::prelude::IsA<gtk4::Widget>) -> ScrolledWindow {
    let scrolled = ScrolledWindow::new();
    scrolled.set_child(Some(child));
    scrolled.set_propagate_natural_height(true);
    scrolled.set_hexpand(true);
    scrolled.set_vexpand(true);
    let _ = Align::Fill;
    scrolled
}
