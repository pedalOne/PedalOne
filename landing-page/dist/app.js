const screens = {
  map: { number: '01 / NAVIGATE', title: 'Less guessing.\nMore going.', description: 'Full-color maps and turn-by-turn directions keep the next turn in view.', alt: 'PedalOne on bicycle handlebars showing turn-by-turn navigation' },
  speed: { number: '02 / RIDE', title: 'Your effort.\nIn full color.', description: 'Your speed front and center, with ride metrics a glance away.', alt: 'PedalOne on bicycle handlebars showing a colorful speedometer and ride metrics' },
  climb: { number: '03 / CLIMB', title: 'A little perspective.\nFor the uphill miles.', description: 'See the climb ahead on a dedicated elevation screen.', alt: 'PedalOne on bicycle handlebars showing its climbing and elevation screen' }
};
document.querySelectorAll('[data-screen]').forEach(button => {
  button.addEventListener('click', () => {
    const key = button.dataset.screen;
    const screen = screens[key];
    document.querySelectorAll('[data-screen]').forEach(item => item.setAttribute('aria-pressed', String(item === button)));
    const image = document.getElementById('screen-image');
    document.getElementById('screen-still').srcset = `assets/${key}.png`;
    image.src = `assets/${key}.gif`;
    image.alt = screen.alt;
    document.getElementById('screen-number').textContent = screen.number;
    document.getElementById('screen-title').textContent = screen.title;
    document.getElementById('screen-description').textContent = screen.description;
  });
});

// Keep the section navigation available and reflect the reader's position.
const header = document.querySelector('header.nav');
const sectionNav = header.querySelector('nav');
const sectionLinks = [...sectionNav.querySelectorAll('a[href^="#"]')];
const navSections = sectionLinks.map(link => document.querySelector(link.getAttribute('href')));
let activeSection = null;
let scrollQueued = false;

function updateSectionNavigation() {
  scrollQueued = false;
  const threshold = header.getBoundingClientRect().height + 48;
  let current = -1;
  navSections.forEach((section, index) => {
    if (section.getBoundingClientRect().top <= threshold) current = index;
  });
  if (current === activeSection) return;
  activeSection = current;
  sectionLinks.forEach((link, index) => {
    if (index === current) link.setAttribute('aria-current', 'location');
    else link.removeAttribute('aria-current');
  });
  // Reveal the selected mobile tab without moving the page vertically.
  if (current >= 0 && sectionNav.scrollWidth > sectionNav.clientWidth) {
    const linkRect = sectionLinks[current].getBoundingClientRect();
    const navRect = sectionNav.getBoundingClientRect();
    if (linkRect.left < navRect.left || linkRect.right > navRect.right) {
      sectionNav.scrollLeft += linkRect.left - navRect.left - 12;
    }
  }
}
function queueSectionUpdate() {
  if (!scrollQueued) {
    scrollQueued = true;
    requestAnimationFrame(updateSectionNavigation);
  }
}
function measureHeader() {
  document.documentElement.style.setProperty('--header-height', `${header.getBoundingClientRect().height}px`);
  queueSectionUpdate();
}
new ResizeObserver(measureHeader).observe(header);
window.addEventListener('scroll', queueSectionUpdate, { passive: true });
window.addEventListener('resize', measureHeader);
window.addEventListener('load', measureHeader);
window.addEventListener('hashchange', queueSectionUpdate);
measureHeader();

// Local design preview: do not store or transmit submitted email addresses.
document.getElementById('updates-form').addEventListener('submit', event => {
  event.preventDefault();
  document.getElementById('signup-status').textContent = 'Preview only — your email has not been saved or subscribed. Signup will be enabled once a mailing-list service is connected.';
});
