// The steps from a ROM to a running game, shown in place of the screen while
// they run: a bar, and each step with its state and time. The bar moves by
// each step's usual share of the time (measured on SMB), and within a step
// by its elapsed time against that share, so that it keeps moving during the
// long ones.

// The recompiler's stages, by the keys src/pipeline reports.
export const RECOMPILER_STEPS = [
    ['map', 'READ THE MAP', 10],
    ['rom', 'READ THE CARTRIDGE', 15],
    ['discover', 'FIND THE CODE', 330],
    ['stacks', 'CHECK STACK USE', 100],
    ['lower', 'TRANSLATE THE INSTRUCTIONS', 950],
    ['ir', 'BUILD THE IR', 15],
    ['resumable', 'MAKE WAIT LOOPS RESUMABLE', 6],
    ['optimize', 'OPTIMISE', 500],
    ['c', 'WRITE THE C', 100],
    ['inline', 'INLINE SMALL ROUTINES', 330],
    ['wasm', 'EMIT WEBASSEMBLY', 40],
];

// [key, label, usual milliseconds]
const STEPS = [
    ['files', 'FETCH THE GAME\'S FILES', 60],
    ['runtime', 'PREPARE THE RUNTIME', 60],
    ['cache', 'LOOK FOR AN EARLIER BUILD', 30],
    ...RECOMPILER_STEPS,
    ['store', 'KEEP THE BUILD FOR NEXT TIME', 30],
    ['link', 'LINK THE GAME', 150],
    ['start', 'START', 100],
];

export function createProgress(container) {
    const panel = document.createElement('div');
    panel.className = 'progress';
    panel.hidden = true;
    const title = document.createElement('h2');
    const bar = document.createElement('div');
    bar.className = 'bar';
    const fill = document.createElement('div');
    bar.append(fill);
    const list = document.createElement('ol');
    panel.append(title, bar, list);
    container.prepend(panel);

    const rows = new Map();
    let current = null; // { key, start }
    let animation = 0;
    const total = STEPS.reduce((sum, [, , weight]) => sum + weight, 0);

    const row = key => rows.get(key);
    const weightOf = key => STEPS.find(([k]) => k === key)?.[2] ?? 0;

    // done and skipped steps count in full; the current one by its elapsed
    // time, up to 90% of its share
    const update = () => {
        let done = 0;
        for (const [key, , weight] of STEPS) {
            const state = row(key).dataset.state;
            if (state === 'done' || state === 'skipped') done += weight;
        }
        if (current) {
            const elapsed = performance.now() - current.start;
            done += Math.min(elapsed / Math.max(weightOf(current.key), 1), 0.9) * weightOf(current.key);
            row(current.key).querySelector('.time').textContent = `${(elapsed / 1000).toFixed(1)} S`;
        }
        fill.style.width = `${Math.min(100, (done / total) * 100).toFixed(1)}%`;
    };
    const tick = () => {
        update();
        animation = requestAnimationFrame(tick);
    };

    // `took`: the step's milliseconds when measured elsewhere (the worker)
    const finishCurrent = (took = performance.now() - current?.start) => {
        if (!current) return;
        const element = row(current.key);
        element.dataset.state = 'done';
        element.querySelector('.time').textContent = `${Math.round(took)} MS`;
        current = null;
    };

    return {
        // Shows the panel with every step to come.
        show(name) {
            title.textContent = `RECOMPILING ${name}`;
            list.replaceChildren();
            rows.clear();
            for (const [key, label] of STEPS) {
                const item = document.createElement('li');
                item.dataset.state = 'pending';
                const text = document.createElement('span');
                text.textContent = label;
                const time = document.createElement('span');
                time.className = 'time';
                item.append(text, time);
                list.append(item);
                rows.set(key, item);
            }
            current = null;
            panel.hidden = false;
            panel.classList.remove('failed');
            cancelAnimationFrame(animation);
            tick();
        },
        // Ends the current step, in `took` milliseconds if given, and starts
        // `key` (steps passed over are marked done: they took no time worth
        // showing).
        begin(key, took) {
            if (!rows.has(key) || panel.hidden) return;
            finishCurrent(took);
            for (const [k] of STEPS) {
                if (k === key) break;
                if (row(k).dataset.state === 'pending') row(k).dataset.state = 'done';
            }
            row(key).dataset.state = 'active';
            current = { key, start: performance.now() };
        },
        // Marks steps that did not need to run, with the reason.
        skip(keys, reason) {
            finishCurrent();
            for (const key of keys) {
                const element = row(key);
                if (!element) continue;
                element.dataset.state = 'skipped';
                element.querySelector('.time').textContent = reason;
            }
        },
        // Shows the step that failed and why; the panel stays.
        fail(message) {
            if (panel.hidden) return;
            if (current) row(current.key).dataset.state = 'failed';
            current = null;
            panel.classList.add('failed');
            title.textContent = message.toUpperCase();
            cancelAnimationFrame(animation);
            update();
        },
        hide() {
            finishCurrent();
            cancelAnimationFrame(animation);
            panel.hidden = true;
        },
        get visible() { return !panel.hidden; },
    };
}
