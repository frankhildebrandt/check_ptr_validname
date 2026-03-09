import { defineConfig } from 'astro/config';
import Unocss from 'unocss/astro';
import icon from 'astro-icon';

export default defineConfig({
  output: 'static',
  integrations: [Unocss({ injectReset: true, injectEntry: true }), icon()]
});
