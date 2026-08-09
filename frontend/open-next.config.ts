import { defineCloudflareConfig } from "@opennextjs/cloudflare";

// No ISR or fetch caching in this app — every page is either static or reads
// live state through /api. "dummy" keeps the deploy free of KV/R2/D1 bindings.
// Add an incremental cache here if a route ever starts using revalidate.
export default defineCloudflareConfig({
  incrementalCache: "dummy",
  tagCache: "dummy",
  queue: "dummy",
});
