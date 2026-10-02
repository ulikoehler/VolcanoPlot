import { defineConfig } from '@playwright/test';

export default defineConfig({
    testDir: 'test',
    timeout: 60_000,
    // Keep browser parallelism low — each test allocates real GPU
    // resources (incl. a 64 MB frame-scratch buffer per interpreter).
    workers: 1,
    use: {
        channel: 'chrome',   // system google-chrome
        launchOptions: {
            args: [
                '--enable-unsafe-webgpu',
                '--enable-features=Vulkan',
                '--no-sandbox',
            ],
            env: {
                ...process.env,
                // Pin WebGPU to the Intel iGPU (SwiftShader software
                // rendering allocates too much RAM and destabilized
                // the host).
                VK_DRIVER_FILES: '/usr/share/vulkan/icd.d/intel_icd.json',
            },
        },
    },
});
