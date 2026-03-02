import { UnrealOpenCodeMCPServer } from './server.js';

const server = new UnrealOpenCodeMCPServer();

process.on('SIGINT', async () => {
  await server.stop();
  process.exit(0);
});

process.on('SIGTERM', async () => {
  await server.stop();
  process.exit(0);
});

server.start().catch((err) => {
  process.stderr.write(`[UnrealOpenCode] Fatal error: ${err}\n`);
  process.exit(1);
});
