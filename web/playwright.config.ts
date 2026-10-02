import { defineConfig } from '@playwright/test';

export default defineConfig({
    testDir: 'test',
    timeout: 60_000,
    use: {
        channel: 'chrome',   // system google-chrome
        launchOptions: {
            args: [
                '--enable-unsafe-webgpu',
                '--use-webgpu-adapter=swiftshader',
                '--enable-features=Vulkan',
                '--no-sandbox',
            ],
        },
    },
});
