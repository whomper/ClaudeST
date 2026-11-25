// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 AERA | Erez Yaary
//
// webgate's extension, in each page: once the page has loaded and its
// scripts have been quiet for a moment, the page as it is now, made into
// plain HTML for EWeb: no scripts, each canvas as a picture, form fields
// as they are filled, and a <base> so that its stylesheets and pictures
// come from where they are. Cloudflare's check ("Just a moment...") is not
// a page to send: the browser passes it and goes on to the page by itself,
// whose own snapshot is sent; one that is still there after CHECK (it
// wants a click) is sent marked so.

(function () {
  const QUIET = 800, MOST = 8000, CHECK = 25000;
  let last = Date.now(), sent = false;
  const started = Date.now();
  new MutationObserver(() => { last = Date.now(); }).observe(document, {subtree: true, childList: true, attributes: true, characterData: true});

  function snapshot() {
    const root = document.documentElement.cloneNode(true);
    // the canvases as pictures, in the clone where the originals were
    const live = document.querySelectorAll("canvas"), copies = root.querySelectorAll("canvas");
    copies.forEach((c, i) => {
      try {
        const img = document.createElement("img");
        img.src = live[i].toDataURL("image/png");
        img.width = live[i].width;
        img.height = live[i].height;
        c.replaceWith(img);
      } catch (e) {
        c.remove();          // a canvas from another site can't be read
      }
    });
    // fields as they are now
    const fields = document.querySelectorAll("input, textarea, select"), fcopies = root.querySelectorAll("input, textarea, select");
    fcopies.forEach((f, i) => {
      const o = fields[i];
      if (o.tagName === "TEXTAREA")
        f.textContent = o.value;
      else if (o.tagName === "SELECT")
        Array.from(f.options).forEach((opt, k) => { if (o.options[k] && o.options[k].selected) opt.setAttribute("selected", ""); else opt.removeAttribute("selected"); });
      else if (o.type === "checkbox" || o.type === "radio") {
        if (o.checked) f.setAttribute("checked", ""); else f.removeAttribute("checked");
      } else if (o.type !== "password" && o.type !== "file")
        f.setAttribute("value", o.value);
    });
    root.querySelectorAll("script, noscript, template, iframe, object, embed").forEach(e => e.remove());
    root.querySelectorAll("*").forEach(e => {
      for (const a of Array.from(e.attributes))
        if (a.name.startsWith("on"))
          e.removeAttribute(a.name);       // the page's own handlers: nothing runs on the Atari
    });
    let head = root.querySelector("head");
    if (!head) {
      head = document.createElement("head");
      root.insertBefore(head, root.firstChild);
    }
    root.querySelectorAll("base").forEach(e => e.remove());
    const base = document.createElement("base");
    base.href = location.href;
    head.insertBefore(base, head.firstChild);
    const meta = document.createElement("meta");
    meta.setAttribute("charset", "utf-8");
    head.insertBefore(meta, head.firstChild);
    return "<!DOCTYPE html>\n" + root.outerHTML;
  }

  // Cloudflare's check: its options in an inline script (the page's own
  // scripts can't be read from here, their text can)
  function checking() {
    return Array.from(document.scripts).some(s => !s.src && s.textContent.includes("_cf_chl_opt"));
  }

  function settle() {
    if (sent)
      return;
    const check = checking();
    if (check ? Date.now() - started < CHECK : Date.now() - last < QUIET && Date.now() - started < MOST) {
      setTimeout(settle, check ? 500 : 200);
      return;
    }
    sent = true;
    chrome.runtime.sendMessage({snapshot: true, challenge: check, url: location.href, title: document.title,
                                html: snapshot()});
  }

  // live view's picture: the links and fields in the view (CSS px from its
  // corner), so that EWeb shows the pointing hand over them, and whether a
  // field has the keyboard, so that what is typed on the Atari goes there
  // (decided 21 Nov 2026)
  const CLICKS = "a[href], button, input:not([type=hidden]), select, textarea, summary, label, " +
                 "[onclick], [role=button], [role=link], [role=tab], [role=menuitem], [contenteditable]";
  function areas() {
    const out = [], vw = window.innerWidth, vh = window.innerHeight;
    for (const e of document.querySelectorAll(CLICKS)) {
      const r = e.getBoundingClientRect();
      const x0 = Math.max(0, r.left), y0 = Math.max(0, r.top);
      const x1 = Math.min(vw, r.right), y1 = Math.min(vh, r.bottom);
      if (x1 - x0 < 2 || y1 - y0 < 2)
        continue;
      const st = getComputedStyle(e);
      if (st.visibility === "hidden" || st.display === "none")
        continue;
      out.push([Math.round(x0), Math.round(y0), Math.round(x1 - x0), Math.round(y1 - y0)]);
      if (out.length >= 400)
        break;
    }
    const a = document.activeElement;
    const typing = !!a && (a.isContentEditable || a.tagName === "TEXTAREA" ||
      (a.tagName === "INPUT" && !/^(button|submit|reset|checkbox|radio|file|image|range|color|hidden)$/i.test(a.type)));
    return {areas: out, typing: typing, dpr: window.devicePixelRatio || 1};
  }

  // live view's "as a page": the snapshot of the page as it is now
  chrome.runtime.onMessage.addListener((msg, sender, reply) => {
    if (msg.snapshot_now)
      chrome.runtime.sendMessage({snapshot: true, url: location.href, title: document.title, html: snapshot()});
    if (msg.areas)
      reply(areas());
  });

  if (document.readyState === "complete")
    settle();
  else
    window.addEventListener("load", settle);
})();
