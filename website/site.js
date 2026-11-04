(function () {
  'use strict';
  var reduce = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  // Scroll reveal: a plain position check on scroll/resize (no observers), so content is never left hidden.
  var items = Array.prototype.slice.call(document.querySelectorAll('.reveal'));
  items.forEach(function (el, i) { el.style.transitionDelay = (i % 3) * 70 + 'ms'; });
  var pending = items.slice(), ticking = false;
  function reveal() {
    ticking = false;
    var limit = window.innerHeight * 0.94;
    pending = pending.filter(function (el) {
      var r = el.getBoundingClientRect();
      if (reduce || r.top < limit) { el.classList.add('in'); return false; }
      return true;
    });
  }
  function schedule() { if (!ticking) { ticking = true; requestAnimationFrame(reveal); } }
  window.addEventListener('scroll', schedule, { passive: true });
  window.addEventListener('resize', schedule);
  window.addEventListener('hashchange', schedule);
  window.addEventListener('load', schedule);
  reveal();
  setTimeout(reveal, 400);

  // Highlight the visitor's platform and tailor the primary button
  var ua = navigator.userAgent || '', plat = (navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || '';
  var os = /Mac/i.test(plat + ua) && !/iPhone|iPad/i.test(ua) ? 'mac' : /Win/i.test(plat + ua) ? 'win' : /Linux|X11/i.test(plat + ua) && !/Android/i.test(ua) ? 'linux' : '';
  var labels = { mac: 'Download for macOS', win: 'Download for Windows', linux: 'Build for Linux' };
  var links = { mac: 'downloads/OpenSc3k-macos-arm64.dmg', win: 'downloads/OpenSc3k-windows-x64.zip', linux: '#build' };
  if (os) {
    var card = document.querySelector('.dl .card[data-os="' + os + '"]');
    if (card) card.classList.add('current');
    document.getElementById('primary-label').textContent = labels[os];
    document.getElementById('primary-dl').setAttribute('href', links[os]);
  }

  // Pointer spotlight on cards
  document.querySelectorAll('.card').forEach(function (c) {
    c.addEventListener('pointermove', function (e) {
      var r = c.getBoundingClientRect();
      c.style.setProperty('--mx', (e.clientX - r.left) + 'px');
      c.style.setProperty('--my', (e.clientY - r.top) + 'px');
    });
  });

  // Hero screenshot tilt
  var stage = document.getElementById('stage'), tilt = document.getElementById('tilt');
  if (stage && tilt && !reduce && window.matchMedia('(hover: hover)').matches) {
    stage.addEventListener('pointermove', function (e) {
      var r = stage.getBoundingClientRect();
      var x = (e.clientX - r.left) / r.width - 0.5, y = (e.clientY - r.top) / r.height - 0.5;
      var wide = window.matchMedia('(min-width: 1000px)').matches, ry = wide ? 7 : 0, rx = wide ? 2 : 0;
      tilt.style.transform = 'perspective(1600px) rotateY(' + (ry + x * 7).toFixed(2) + 'deg) rotateX(' + (rx - y * 6).toFixed(2) + 'deg)';
    });
    stage.addEventListener('pointerleave', function () { tilt.style.transform = ''; });
  }

  // Copy buttons
  document.querySelectorAll('.copy').forEach(function (b) {
    b.addEventListener('click', function () {
      var code = document.getElementById('code-' + b.getAttribute('data-copy'));
      if (!code || !navigator.clipboard) return;
      navigator.clipboard.writeText(code.textContent).then(function () {
        var old = b.textContent; b.textContent = 'Copied'; setTimeout(function () { b.textContent = old; }, 1400);
      });
    });
  });

  // Lightbox
  var lb = document.getElementById('lightbox'), img = document.getElementById('lb-img'), cap = document.getElementById('lb-cap');
  var shots = Array.prototype.slice.call(document.querySelectorAll('#gallery button'));
  var cur = 0, lastFocus = null;
  function show(i) {
    cur = (i + shots.length) % shots.length;
    img.src = shots[cur].getAttribute('data-full'); img.alt = shots[cur].getAttribute('data-cap');
    cap.textContent = shots[cur].getAttribute('data-cap');
  }
  function open(i) { lastFocus = document.activeElement; show(i); lb.classList.add('open'); lb.querySelector('.close').focus(); }
  function close() { lb.classList.remove('open'); if (lastFocus) lastFocus.focus(); }
  shots.forEach(function (b, i) { b.addEventListener('click', function () { open(i); }); });
  lb.querySelector('.close').addEventListener('click', close);
  lb.querySelector('.prev').addEventListener('click', function () { show(cur - 1); });
  lb.querySelector('.next').addEventListener('click', function () { show(cur + 1); });
  lb.addEventListener('click', function (e) { if (e.target === lb) close(); });
  document.addEventListener('keydown', function (e) {
    if (!lb.classList.contains('open')) return;
    if (e.key === 'Escape') close();
    else if (e.key === 'ArrowLeft') show(cur - 1);
    else if (e.key === 'ArrowRight') show(cur + 1);
  });
})();
