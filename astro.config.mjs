import { defineConfig } from 'astro/config';
import Unocss from 'unocss/astro';
import icon from 'astro-icon';

const site = process.env.SITE_URL || 'https://frankhildebrandt.github.io';
const base = process.env.BASE_PATH || '/';

export default defineConfig({
  output: 'static',
  site,
  base,
  integrations: [Unocss({ injectReset: true, injectEntry: true }), icon()]
});
