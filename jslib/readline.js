// readline: interactive line editing with history and tab completion.
//
//   const rl = new LineEditor({ complete: (line, pos) => ({ start, items }) });
//   const line = rl.read('$ ');   // string, or null on ^D at an empty line
import * as sys from 'sys';
import tty from 'tty';
import { columns } from 'fmt';

export class LineEditor {
    constructor({ history = [], complete = null, maxHistory = 200 } = {}) {
        this.history = history;
        this.complete = complete;
        this.maxHistory = maxHistory;
    }

    addHistory(line) {
        if (line.trim() && this.history[this.history.length - 1] !== line) {
            this.history.push(line);
            if (this.history.length > this.maxHistory) this.history.shift();
        }
    }

    read(prompt = '> ') {
        if (!sys.isatty(0)) return this.readCooked(prompt);
        let buf = '', pos = 0, hidx = this.history.length, saved = '';
        let lastTab = false;
        const out = (s) => sys.write(1, s);
        const promptWidth = tty.width(prompt);
        const redraw = () => {
            out('\r' + prompt + buf + tty.clearEol + tty.left(buf.length - pos));
        };
        tty.raw(true);
        out(prompt);
        try {
            for (;;) {
                const k = tty.readKey();
                const tab = k === 'tab';
                if (k === null || (k === 'ctrl-d' && buf === '')) {
                    out('\n');
                    return null;
                }
                switch (k) {
                    case 'enter':
                        out('\n');
                        return buf;
                    case 'ctrl-c':
                        out('^C\n');
                        return '';
                    case 'backspace':
                        if (pos > 0) { buf = buf.slice(0, pos - 1) + buf.slice(pos); pos--; }
                        break;
                    case 'delete': case 'ctrl-d':
                        buf = buf.slice(0, pos) + buf.slice(pos + 1);
                        break;
                    case 'left': case 'ctrl-b': if (pos > 0) pos--; break;
                    case 'right': case 'ctrl-f': if (pos < buf.length) pos++; break;
                    case 'home': case 'ctrl-a': pos = 0; break;
                    case 'end': case 'ctrl-e': pos = buf.length; break;
                    case 'ctrl-k': buf = buf.slice(0, pos); break;
                    case 'ctrl-u': buf = buf.slice(pos); pos = 0; break;
                    case 'ctrl-w': {
                        let i = pos;
                        while (i > 0 && buf[i - 1] === ' ') i--;
                        while (i > 0 && buf[i - 1] !== ' ') i--;
                        buf = buf.slice(0, i) + buf.slice(pos);
                        pos = i;
                        break;
                    }
                    case 'ctrl-l':
                        out(tty.clearScreen);
                        break;
                    case 'up':
                        if (hidx > 0) {
                            if (hidx === this.history.length) saved = buf;
                            buf = this.history[--hidx];
                            pos = buf.length;
                        }
                        break;
                    case 'down':
                        if (hidx < this.history.length) {
                            hidx++;
                            buf = hidx === this.history.length ? saved : this.history[hidx];
                            pos = buf.length;
                        }
                        break;
                    case 'tab':
                        if (this.complete) {
                            const r = this.doComplete(buf, pos, lastTab, prompt);
                            buf = r.buf;
                            pos = r.pos;
                        }
                        break;
                    default:
                        if (k.length === 1) {
                            buf = buf.slice(0, pos) + k + buf.slice(pos);
                            pos++;
                            if (pos === buf.length) { out(k); lastTab = false; continue; }
                        }
                }
                lastTab = tab;
                redraw();
            }
        } finally {
            tty.raw(false);
        }
    }

    doComplete(buf, pos, listAll, prompt) {
        const { start = pos, items = [] } = this.complete(buf.slice(0, pos), pos) || {};
        if (!items.length) return { buf, pos };
        const word = buf.slice(start, pos);
        let common = items[0];
        for (const it of items) {
            let i = 0;
            while (i < common.length && i < it.length && common[i] === it[i]) i++;
            common = common.slice(0, i);
        }
        if (items.length === 1 && !common.endsWith('/')) common += ' ';
        if (common.length > word.length) {
            buf = buf.slice(0, start) + common + buf.slice(pos);
            return { buf, pos: start + common.length };
        }
        if (items.length > 1 && listAll) {
            const names = items.map(s => s.replace(/\/$/, '').split('/').pop() + (s.endsWith('/') ? '/' : ''));
            sys.write(1, '\n' + columns(names, sys.ttySize().cols) + '\n' + prompt);
        }
        return { buf, pos };
    }

    // non-interactive input (pipes, files)
    readCooked(prompt) {
        if (!this._lines) this._lines = { buf: '', eof: false };
        const st = this._lines;
        for (;;) {
            const i = st.buf.indexOf('\n');
            if (i >= 0) {
                const line = st.buf.slice(0, i);
                st.buf = st.buf.slice(i + 1);
                return line;
            }
            if (st.eof) {
                if (!st.buf) return null;
                const line = st.buf;
                st.buf = '';
                return line;
            }
            const s = sys.read(0, 4096);
            if (s === '') st.eof = true;
            else st.buf += s;
        }
    }
}

export default { LineEditor };
