// About Kiwi.
import * as sys from 'sys';
import * as kernel from 'kernel';
import kz from 'kz';
import fmt from 'fmt';
import { Window, Button, drawIcon, C } from 'gui';

export default {
    id: 'about', title: 'About Kiwi', icon: 'kiwi', single: true,
    create() {
        const u = sys.uname(), z = kz.stats(), c = kernel.cpu(), m = sys.memoryUsage();
        const win = new Window({ title: 'About Kiwi', x: 230, y: 140, w: 470, h: 290, icon: 'kiwi', resizable: false });
        const lines = [
            [`${u.sysname} ${u.release}`, 1],
            ['A tiny Unix-like kernel with compressed', 0],
            ['memory and a JavaScript desktop.', 0],
            ['', 0],
            [`CPU: ${c.brand.slice(0, 40)}`, 0],
            [`Memory: ${fmt.bytes(z.ram)}, ${fmt.bytes(z.free)} free`, 0],
            [`zram: ${fmt.bytes(z.zOrig)} in ${fmt.bytes(z.z_bytes)} (${fmt.ratio(z.zOrig, z.z_bytes)})`, 0],
            [`Files: ${fmt.bytes(z.fs_bytes)} in ${fmt.bytes(z.fs_zbytes)} (${fmt.ratio(z.fs_bytes, z.fs_zbytes)})`, 0],
            [`This desktop: QuickJS, ${fmt.bytes(m.malloc)} heap`, 0],
        ];
        win.paintClient = (g, w) => {
            drawIcon(g, 'kiwi', 18, 18, 64);
            lines.forEach(([t, bold], i) => g.text(100, 14 + i * 18, t, C.text, -1, bold));
            g.bevel(100, 14 + lines.length * 18 + 4, w - 112, 2, 6);
        };
        const ok = win.add(new Button(296, 220, 80, 24, 'OK', () => win.close()));
        win.layout = (cw, ch) => ok.move(cw - 88, ch - 32);
        win.focus = ok;
        return win;
    },
};
