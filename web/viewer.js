// A small code viewer with tabs, written for this page.
//
// - Generated files are read-only and virtualised: only the lines in view
//   are in the DOM, so the 12,000 lines of code.c scroll smoothly.
// - The map is editable (a textarea over a highlighted copy of its text);
//   Recompile regenerates the game and the C from it.
// - Search with the box or Ctrl+F (Enter, Shift+Enter); click a routine name
//   to go to its definition, Alt+Left to come back.

const LINE_HEIGHT = 18;
const OVERSCAN = 20;

const C_KEYWORDS = new Set([
    'void', 'return', 'if', 'else', 'do', 'while', 'for', 'switch', 'case', 'default',
    'goto', 'static', 'const', 'extern', 'break', 'continue', 'sizeof', 'true', 'false',
]);
const C_TYPES = new Set(['uint8_t', 'uint16_t', 'int', 'bool', 'char', 'unsigned']);
const MACHINE = new Set([
    'a', 'x', 'y', 'sp', 'carry_flag', 'zero_flag', 'neg_flag', 'overflow_flag',
    'interrupt_disabled', 'decimal_flag', 'ram', 'data', 'ROM', 'read_byte',
    'dynamic_ram_write', 'video', 'return_address',
]);

function escape(text) {
    return text.replace(/[&<>]/g, c => (c === '&' ? '&amp;' : c === '<' ? '&lt;' : '&gt;'));
}

function span(cls, text) {
    return `<span class="${cls}">${escape(text)}</span>`;
}

const C_TOKEN = /(\/\/.*$)|("(?:\\.|[^"\\])*")|(\b0x[0-9a-fA-F]+\b|\b\d+\b)|([A-Za-z_]\w*)|(\s+)|([^\sA-Za-z_0-9"])/g;

export function highlightC(line) {
    const directive = /^(\s*)(#\s*\w+)(.*)$/.exec(line);
    if (directive) {
        return escape(directive[1]) + span('pre', directive[2]) + highlightC(directive[3]);
    }
    const label = /^(\w+):(;?)$/.exec(line);
    if (label) return span('label', label[1] + ':' + label[2]);
    let out = '';
    let match;
    C_TOKEN.lastIndex = 0;
    while ((match = C_TOKEN.exec(line)) !== null) {
        const [text, comment, string, number, ident] = match;
        if (comment !== undefined) out += span('comment', text);
        else if (string !== undefined) out += span('string', text);
        else if (number !== undefined) out += span('number', text);
        else if (ident !== undefined) {
            const call = line[C_TOKEN.lastIndex] === '(' || line.startsWith(' (', C_TOKEN.lastIndex);
            if (C_KEYWORDS.has(text)) out += span('keyword', text);
            else if (C_TYPES.has(text)) out += span('type', text);
            else if (MACHINE.has(text)) out += span('machine', text);
            else if (call) out += `<span class="call" data-name="${text}">${text}</span>`;
            else if (/^[A-Z][A-Z0-9_]+$/.test(text)) out += span('const', text);
            else out += escape(text);
        } else out += escape(text);
    }
    return out;
}

const MAP_LINE = /^(\s*)(routine|table|var|io)(\s+)(\$[0-9a-fA-F]+)(\s+)(\S+)((?:\s+\d+)?)(\s*)(;.*)?$/;

export function highlightMap(line) {
    if (/^\s*#/.test(line)) return span('comment', line);
    const m = MAP_LINE.exec(line);
    if (!m) return escape(line);
    const [, lead, kind, s1, address, s2, name, length, s3, comment] = m;
    return escape(lead) + span(`kind kind-${kind}`, kind) + escape(s1) + span('number', address) + escape(s2) +
        span(kind === 'routine' ? 'call' : 'const', name) + span('number', length) + escape(s3) +
        (comment ? span('comment', comment) : '');
}

function el(tag, cls, text) {
    const node = document.createElement(tag);
    if (cls) node.className = cls;
    if (text !== undefined) node.textContent = text;
    return node;
}

export function createViewer(root, { onRecompile, onResetMap }) {
    root.classList.add('viewer');
    const tabs = el('div', 'tabs');
    const toolbar = el('div', 'toolbar');
    const search = el('input', 'search');
    search.placeholder = 'SEARCH';
    search.spellcheck = false;
    const searchInfo = el('span', 'search-info');
    const back = el('button', 'tool', '< BACK');
    back.title = 'Back (Alt+Left)';
    const recompile = el('button', 'tool action', 'RECOMPILE');
    const resetMap = el('button', 'tool', 'RESET MAP');
    toolbar.append(back, search, searchInfo, el('span', 'spacer'), resetMap, recompile);
    const body = el('div', 'body');
    const status = el('div', 'status');
    root.append(tabs, toolbar, body, status);

    let files = [];
    let current = null; // { file, view }
    const views = new Map();
    const history = [];

    function definitionOf(name) {
        const pattern = new RegExp(`^(?:void|static uint16_t) ${name}\\(`);
        for (const file of files) {
            if (file.editable) continue;
            const lines = file.lines ?? (file.lines = file.contents.split('\n'));
            const index = lines.findIndex(line => pattern.test(line));
            if (index >= 0) return { file, line: index };
        }
        return null;
    }

    function readOnlyView(file) {
        const lines = file.lines ?? (file.lines = file.contents.split('\n'));
        const highlight = file.name.endsWith('.map') ? highlightMap : highlightC;
        const viewport = el('div', 'viewport');
        viewport.tabIndex = 0;
        const spacer = el('div', 'spacer-lines');
        spacer.style.height = `${lines.length * LINE_HEIGHT + 8}px`;
        const content = el('div', 'lines');
        viewport.append(spacer, content);
        const gutter = String(lines.length).length;
        let match = null; // { line, start, end }
        let rendered = '';

        function render() {
            const first = Math.max(0, Math.floor(viewport.scrollTop / LINE_HEIGHT) - OVERSCAN);
            const last = Math.min(lines.length, Math.ceil((viewport.scrollTop + viewport.clientHeight) / LINE_HEIGHT) + OVERSCAN);
            const key = `${first}:${last}:${match ? match.line + ':' + match.start : ''}`;
            if (key === rendered) return;
            rendered = key;
            let html = '';
            for (let i = first; i < last; i++) {
                let source = lines[i];
                let code;
                if (match && match.line === i) {
                    code = highlight(source.slice(0, match.start)) +
                        `<mark>${escape(source.slice(match.start, match.end))}</mark>` +
                        highlight(source.slice(match.end));
                } else {
                    code = highlight(source);
                }
                html += `<div class="line"><span class="no">${String(i + 1).padStart(gutter)}</span>${code || ' '}</div>`;
            }
            content.style.top = `${first * LINE_HEIGHT}px`;
            content.innerHTML = html;
        }

        viewport.addEventListener('scroll', render);
        new ResizeObserver(render).observe(viewport);
        content.addEventListener('click', event => {
            const target = event.target.closest('.call');
            if (!target) return;
            const found = definitionOf(target.dataset.name);
            if (found) goTo(found.file, found.line);
        });

        return {
            element: viewport,
            render,
            scrollTo(line) {
                viewport.scrollTop = Math.max(0, line * LINE_HEIGHT - viewport.clientHeight / 3);
                render();
            },
            position: () => Math.floor(viewport.scrollTop / LINE_HEIGHT),
            find(query, from, step) {
                if (!query) {
                    match = null;
                    rendered = '';
                    render();
                    return null;
                }
                const needle = query.toLowerCase();
                for (let k = 1; k <= lines.length; k++) {
                    const i = (from + k * step + lines.length * 2) % lines.length;
                    const at = lines[i].toLowerCase().indexOf(needle);
                    if (at >= 0) {
                        match = { line: i, start: at, end: at + query.length };
                        rendered = '';
                        this.scrollTo(i);
                        return i;
                    }
                }
                match = null;
                rendered = '';
                render();
                return null;
            },
            current: () => (match ? match.line : -1),
        };
    }

    function editableView(file) {
        const wrap = el('div', 'editor');
        const gutter = el('pre', 'gutter');
        const layers = el('div', 'layers');
        const pre = el('pre', 'highlight');
        const area = el('textarea', 'source');
        area.spellcheck = false;
        area.value = file.contents;
        area.wrap = 'off';
        layers.append(pre, area);
        wrap.append(gutter, layers);

        function render() {
            const lines = area.value.split('\n');
            pre.innerHTML = lines.map(highlightMap).join('\n') + '\n';
            gutter.textContent = lines.map((_, i) => i + 1).join('\n') + '\n';
            file.contents = area.value;
            file.dirty = area.value !== file.original;
            updateTools();
        }
        area.addEventListener('input', render);
        area.addEventListener('scroll', () => {
            pre.style.transform = `translate(${-area.scrollLeft}px, ${-area.scrollTop}px)`;
            gutter.scrollTop = area.scrollTop;
        });
        render();
        return {
            element: wrap,
            render() {},
            scrollTo(line) {
                area.scrollTop = Math.max(0, line * LINE_HEIGHT - area.clientHeight / 3);
            },
            position: () => Math.floor(area.scrollTop / LINE_HEIGHT),
            find(query, from, step) {
                if (!query) return null;
                const lines = area.value.split('\n');
                const needle = query.toLowerCase();
                for (let k = 1; k <= lines.length; k++) {
                    const i = (from + k * step + lines.length * 2) % lines.length;
                    const at = lines[i].toLowerCase().indexOf(needle);
                    if (at >= 0) {
                        const offset = lines.slice(0, i).reduce((n, l) => n + l.length + 1, 0) + at;
                        area.focus();
                        area.setSelectionRange(offset, offset + query.length);
                        this.scrollTo(i);
                        search.focus();
                        lastFound = i;
                        return i;
                    }
                }
                return null;
            },
            current: () => lastFound,
            reset(text) {
                area.value = text;
                render();
            },
        };
    }
    let lastFound = -1;

    function updateTools() {
        const editing = current && current.file.editable;
        recompile.hidden = !editing;
        resetMap.hidden = !editing;
        recompile.disabled = !(editing && current.file.dirty);
        resetMap.disabled = !(editing && current.file.contents !== current.file.default);
        back.disabled = history.length === 0;
    }

    function show(file) {
        if (!views.has(file.name)) {
            views.set(file.name, file.editable ? editableView(file) : readOnlyView(file));
        }
        current = { file, view: views.get(file.name) };
        body.replaceChildren(current.view.element);
        current.view.render();
        for (const tab of tabs.children) tab.classList.toggle('active', tab.dataset.name === file.name);
        searchInfo.textContent = '';
        updateTools();
    }

    function goTo(file, line) {
        if (current) history.push({ file: current.file, line: current.view.position() });
        show(file);
        current.view.scrollTo(line);
        updateTools();
    }

    function find(step) {
        if (!current) return;
        const from = current.view.current();
        const found = current.view.find(search.value, from < 0 ? (step > 0 ? -1 : 0) : from, step);
        searchInfo.textContent = search.value && found === null ? 'NOT FOUND' : found === null ? '' : `LINE ${found + 1}`;
    }

    search.addEventListener('keydown', event => {
        if (event.key === 'Enter') find(event.shiftKey ? -1 : 1);
        if (event.key === 'Escape') {
            search.value = '';
            find(1);
            current?.view.element.focus();
        }
    });
    search.addEventListener('input', () => {
        if (!current || current.file.editable) return;
        const found = current.view.find(search.value, current.view.position() - 1, 1);
        searchInfo.textContent = search.value && found === null ? 'NOT FOUND' : '';
    });
    root.addEventListener('keydown', event => {
        if ((event.ctrlKey || event.metaKey) && event.key === 'f') {
            event.preventDefault();
            search.focus();
            search.select();
        }
        if (event.altKey && event.key === 'ArrowLeft') goBack();
    });
    function goBack() {
        const previous = history.pop();
        if (!previous) return;
        show(previous.file);
        current.view.scrollTo(previous.line);
        updateTools();
    }
    back.addEventListener('click', goBack);
    recompile.addEventListener('click', () => {
        if (current?.file.editable) onRecompile(current.file.contents);
    });
    resetMap.addEventListener('click', () => {
        if (!current?.file.editable) return;
        current.view.reset(current.file.default);
        onResetMap();
    });

    return {
        // files: [{ name, contents, editable?, default? }]; the open tab and
        // edits to editable files are kept
        setFiles(next) {
            const previous = current?.file.name;
            const kept = new Map(files.filter(f => f.editable).map(f => [f.name, f]));
            files = next.map(file => {
                const old = kept.get(file.name);
                if (old) {
                    old.original = file.contents;
                    old.default = file.default ?? old.default;
                    old.dirty = old.contents !== old.original;
                    return old;
                }
                return { ...file, original: file.contents };
            });
            for (const name of [...views.keys()]) {
                if (!kept.has(name)) views.delete(name);
            }
            history.length = 0;
            tabs.replaceChildren(...files.map(file => {
                const tab = el('button', 'tab', file.name);
                tab.dataset.name = file.name;
                if (file.editable) tab.classList.add('editable');
                tab.addEventListener('click', () => show(file));
                return tab;
            }));
            show(files.find(f => f.name === previous) ?? files[0]);
        },
        setStatus(text, kind = '') {
            status.textContent = text;
            status.className = `status ${kind}`;
        },
        refresh: () => current?.view.render(),
    };
}
