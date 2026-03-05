const tickerItems = [
  "Booting under QEMU",
  "IPC tests: 13/13 passing",
  "Shell prompt: unhox$",
  "Server split: BSD + VFS + Net + Device + Auth",
  "Road to v1.0 desktop"
];

let tickerIndex = 0;
const tickerText = document.getElementById("ticker-text");

setInterval(() => {
  tickerIndex = (tickerIndex + 1) % tickerItems.length;
  if (tickerText) {
    tickerText.textContent = tickerItems[tickerIndex];
  }
}, 2400);

const revealNodes = document.querySelectorAll(".reveal");
const revealObserver = new IntersectionObserver(
  (entries) => {
    entries.forEach((entry) => {
      if (entry.isIntersecting) {
        entry.target.classList.add("is-visible");
      }
    });
  },
  { threshold: 0.12 }
);

revealNodes.forEach((node, index) => {
  node.style.transitionDelay = `${Math.min(index * 80, 320)}ms`;
  revealObserver.observe(node);
});

const countNodes = document.querySelectorAll(".count");
const countObserver = new IntersectionObserver(
  (entries, observer) => {
    entries.forEach((entry) => {
      if (!entry.isIntersecting) {
        return;
      }

      const node = entry.target;
      const target = Number(node.dataset.target || 0);
      const durationMs = 1000;
      const start = performance.now();

      const animate = (now) => {
        const progress = Math.min((now - start) / durationMs, 1);
        const value = Math.floor(progress * target);
        node.textContent = String(value);

        if (progress < 1) {
          requestAnimationFrame(animate);
        } else {
          node.textContent = String(target);
        }
      };

      requestAnimationFrame(animate);
      observer.unobserve(node);
    });
  },
  { threshold: 0.5 }
);

countNodes.forEach((node) => countObserver.observe(node));
