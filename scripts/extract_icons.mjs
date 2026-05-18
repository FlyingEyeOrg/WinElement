import { readFileSync, existsSync, writeFileSync, mkdirSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const indexPath = join(__dirname, '..', 'node_modules', '@element-plus', 'icons-vue', 'dist', 'index.js');
const content = readFileSync(indexPath, 'utf-8');

const sections = content.split('// src/components/');
sections.shift(); // Remove everything before first icon

const icons = [];

for (const section of sections) {
  const nameMatch = section.match(/^([\w-]+)\.vue/);
  if (!nameMatch) continue;
  
  const iconName = nameMatch[1];
  
  // Extract all 'd' attribute values from path elements
  const pathRegex = /d:\s*"([^"]+)"/g;
  const paths = [];
  let pathMatch;
  while ((pathMatch = pathRegex.exec(section)) !== null) {
    paths.push(pathMatch[1]);
  }
  
  if (paths.length === 0) continue;
  
  // Convert kebab-case to PascalCase for C++ enum/class names
  const pascalName = iconName
    .split('-')
    .map(part => part.charAt(0).toUpperCase() + part.slice(1))
    .join('');
  
  icons.push({
    kebab: iconName,
    pascal: pascalName,
    paths: paths
  });
}

// Sort alphabetically
icons.sort((a, b) => a.pascal.localeCompare(b.pascal));

console.log(`Found ${icons.length} icons`);

// Write the JSON file for inspection
writeFileSync(join(__dirname, '..', 'scripts', 'element-icons.json'), JSON.stringify(icons, null, 2));
console.log('Written to scripts/element-icons.json');

// Now generate the C++ header file
function escapeForCpp(str) {
  return str.replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n');
}

function sv(value) {
  return 'std::string_view{"' + escapeForCpp(value) + '"}';
}

let headerContent = `#pragma once

// Auto-generated from @element-plus/icons-vue v2.3.2
// Contains SVG path data for all Element Plus icons.
// All icons use viewBox="0 0 1024 1024" with fill="currentColor".
//
// Usage:
//   #include <winelement/elements/all_icons.hpp>
//   // Access by name:
//   auto& icon = winelement::elements::icons::ArrowDown;
//   // icon.name == "ArrowDown", icon.kebab_name == "arrow-down"
//   // for (auto path : icon.paths) { ... }

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace winelement::elements::icons {

// Non-template base for type-erased access (via pointer-to-base).
struct IconPathsBase {
    std::string_view name;
    std::string_view kebab_name;
    std::span<const std::string_view> paths;

    [[nodiscard]] constexpr std::size_t path_count() const noexcept { return paths.size(); }
};

template <std::size_t N>
struct IconPaths : IconPathsBase {
    std::array<std::string_view, N> paths_storage;

    [[nodiscard]] constexpr std::size_t path_count() const noexcept { return N; }
};

`;

for (const icon of icons) {
  headerContent += `inline constexpr IconPaths<${icon.paths.length}> ${icon.pascal}{\n`;
  headerContent += `    ${sv(icon.pascal)},  // name\n`;
  headerContent += `    ${sv(icon.kebab)},  // kebab_name\n`;
  headerContent += `    {${icon.paths.map(p => sv(p)).join(', ')}},  // paths_storage\n`;
  headerContent += `};\n\n`;
}

// Append runtime lookup table
headerContent += `struct IconTableEntry {
    std::string_view name;
    std::string_view kebab_name;
    const IconPathsBase* data;
};

inline std::span<const IconTableEntry> all_icons() noexcept {
    static const IconTableEntry kAllIcons[] = {
`;

for (const icon of icons) {
  headerContent += `        {std::string_view{"${icon.pascal}"}, std::string_view{"${icon.kebab}"}, &${icon.pascal}},\n`;
}

headerContent += `    };
    return kAllIcons;
}

} // namespace winelement::elements::icons
`;

const headerPath = join(__dirname, '..', 'include', 'winelement', 'elements', 'all_icons.hpp');
writeFileSync(headerPath, headerContent);
console.log('Written to', headerPath);