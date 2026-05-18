import { readdirSync, existsSync, readFileSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const iconsDir = join(__dirname, '..', 'node_modules', '@element-plus', 'icons-vue', 'dist', 'es', 'icons');

console.error('Looking for icons in:', iconsDir);
console.error('Exists:', existsSync(iconsDir));

if (!existsSync(iconsDir)) {
  // Try alternate locations
  const alt1 = join(__dirname, '..', 'node_modules', '@element-plus', 'icons-vue', 'dist', 'es');
  console.error('Alt path:', alt1, 'Exists:', existsSync(alt1));
  if (existsSync(alt1)) {
    const items = readdirSync(alt1);
    console.error('Contents:', items.slice(0, 30));
  }
  process.exit(1);
}

const iconFiles = readdirSync(iconsDir).filter(f => f.endsWith('.mjs'));
console.log(JSON.stringify(iconFiles));