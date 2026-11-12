// Navegação por sistema preserva os campos e as funções do firmware existente.
const sections = [];
let section;
for (const node of [...document.querySelector('main').children]) {
  if (node.tagName === 'H2') {
    section = document.createElement('section');
    section.className = 'config-section'; section.id = `system-${sections.length}`;
    node.before(section); sections.push(section);
  }
  if (section) section.append(node);
}
const names = ['Testes de atuadores', 'Sensores e dosador', 'Temperatura', 'Mapa de ignição', 'Ignição e limites'];
const tabs = document.getElementById('config-tabs');
function activateSection(index) {
  sections.forEach((s, i) => s.hidden = i !== index);
  [...tabs.children].forEach((b, i) => { b.setAttribute('aria-selected', i === index); b.tabIndex = i === index ? 0 : -1; });
  sessionStorage.setItem('ecu-section', index);
}
sections.forEach((s, i) => {
  const b = document.createElement('button'); b.textContent = names[i];
  b.id = `tab-${i}`; b.setAttribute('role','tab'); b.setAttribute('aria-controls',s.id);
  s.setAttribute('role','tabpanel'); s.setAttribute('aria-labelledby',b.id);
  b.onclick = () => activateSection(i);
  b.onkeydown = e => {
    if (!['ArrowLeft','ArrowRight','Home','End'].includes(e.key)) return;
    e.preventDefault();
    const n = e.key === 'Home' ? 0 : e.key === 'End' ? sections.length-1 : (i+(e.key==='ArrowRight'?1:-1)+sections.length)%sections.length;
    activateSection(n); tabs.children[n].focus();
  };
  tabs.append(b);
});
activateSection(Math.min(sections.length-1, Math.max(0, Number(sessionStorage.getItem('ecu-section') ?? 3))));
document.querySelectorAll('.form-grid label').forEach(label => {
  const input = label.parentElement.querySelector('input');
  if (input?.id) label.htmlFor = input.id;
});
