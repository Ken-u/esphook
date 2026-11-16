"""Focused-window USB keyboard controller; also embeddable on an existing link."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[3]/"framework/pc"))
from esphook_sdk import AppLink

class KeyController:
    def __init__(self,link,config=None,*,poll=True):
        try:import tkinter as tk
        except ImportError as exc:raise RuntimeError("Tkinter is unavailable; use run.py input tap/hold/triple instead") from exc
        self.tk=tk;self.link=link;self.closed=False;self.down=set();self.releases={};self.poll_link=poll
        if config:link.set_bindings(config)
        self.root=tk.Tk();self.root.title("esphook USB keys");self.root.geometry("440x220")
        tk.Label(self.root,text="Space = K1, Return = K2, Tab = K3\nHold a key for confirmation; triple K1 opens menu.\nOnly this focused window captures keys.").pack(pady=12)
        buttons=tk.Frame(self.root);buttons.pack()
        for title,action in (("Tap K1",lambda:link.tap(0)),("Hold K1",lambda:link.hold(0)),("Triple K1",lambda:link.triple(0)),("Menu",lambda:link.request(dict(op="menu"),channel="app"))):
            tk.Button(buttons,text=title,command=lambda fn=action:self.invoke(fn)).pack(side="left",padx=3)
        self.status=tk.StringVar(value="Connected; focus this window to send keys.")
        tk.Label(self.root,textvariable=self.status,wraplength=420).pack(pady=14)
        self.root.bind("<KeyPress>",self.press);self.root.bind("<KeyRelease>",self.release)
        self.root.bind("<FocusOut>",lambda e:self.cancel())
        self.root.protocol("WM_DELETE_WINDOW",self.close)
        if poll:self.root.after(50,self.read)

    def invoke(self,fn):
        try:fn()
        except (OSError,RuntimeError,ValueError,TimeoutError) as exc:self.status.set(str(exc))

    def press(self,event):
        name=event.keysym.casefold()
        if name in self.releases:
            self.root.after_cancel(self.releases.pop(name));return "break"
        if name not in self.down:
            try:self.link.key_down(name)
            except ValueError:return None
            except (OSError,RuntimeError,TimeoutError) as exc:self.status.set(str(exc));return "break"
            self.down.add(name)
        return "break"

    def release(self,event):
        name=event.keysym.casefold()
        if name not in self.down:return None
        def finish():
            self.releases.pop(name,None);self.down.discard(name);self.invoke(lambda:self.link.key_up(name))
        self.releases[name]=self.root.after_idle(finish)
        return "break"

    def cancel(self):
        for job in self.releases.values():self.root.after_cancel(job)
        self.releases.clear();self.down.clear()
        if getattr(self.link,"_usb_pressed",{}):self.invoke(self.link.release_keys)

    def read(self):
        if self.closed:return
        self.invoke(lambda:self.link.poll(0.001))
        self.root.after(50,self.read)

    def pump(self):
        if not self.closed:
            try:self.root.update()
            except self.tk.TclError:self.closed=True

    def close(self):
        if self.closed:return
        self.cancel();self.closed=True;self.root.destroy()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--port");p.add_argument("--config",type=Path);p.add_argument("--app");p.add_argument("--dry-run",action="store_true");a=p.parse_args()
    if a.dry_run:print(json.dumps(dict(bindings=str(a.config) if a.config else "default",app=a.app)));return
    link=AppLink(a.port);controller=None
    try:
        if a.app:link.open(a.app);link.wait_active(a.app)
        controller=KeyController(link,a.config);controller.root.mainloop()
    finally:
        if controller:controller.close()
        link.close()
if __name__=="__main__":main()
