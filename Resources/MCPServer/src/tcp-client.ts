import * as net from 'net';
import { randomUUID } from 'crypto';
import { UERequest, UEResponse, UEResponseSchema } from './types/protocol.js';
import { DEFAULTS } from './types/constants.js';

interface PendingRequest {
  resolve: (response: UEResponse) => void;
  reject: (error: Error) => void;
  timeout: NodeJS.Timeout;
}

export class UETCPClient {
  private socket: net.Socket | null = null;
  private pendingRequests = new Map<string, PendingRequest>();
  private receiveBuffer = Buffer.alloc(0);
  private connected = false;
  private reconnectAttempts = 0;

  private readonly host: string;
  private readonly port: number;
  private readonly maxReconnectAttempts: number;
  private readonly reconnectInterval: number;
  private readonly requestTimeout: number;
  private readonly connectionTimeout: number;

  constructor() {
    this.host = process.env.UEOC_HOST ?? DEFAULTS.TCP_HOST;
    this.port = parseInt(process.env.UEOC_PORT ?? String(DEFAULTS.TCP_PORT), 10);
    this.maxReconnectAttempts = parseInt(
      process.env.UEOC_RECONNECT_ATTEMPTS ?? String(DEFAULTS.RECONNECT_ATTEMPTS),
      10,
    );
    this.reconnectInterval = parseInt(
      process.env.UEOC_RECONNECT_INTERVAL ?? String(DEFAULTS.RECONNECT_INTERVAL_MS),
      10,
    );
    this.requestTimeout = parseInt(
      process.env.UEOC_REQUEST_TIMEOUT ?? String(DEFAULTS.REQUEST_TIMEOUT_MS),
      10,
    );
    this.connectionTimeout = DEFAULTS.CONNECTION_TIMEOUT_MS;
  }

  // ─── Public API ───────────────────────────────────────────────

  async connect(): Promise<void> {
    return new Promise<void>((resolve, reject) => {
      this.socket = new net.Socket();

      const connectionTimer = setTimeout(() => {
        this.socket?.destroy();
        reject(new Error(`Connection timeout after ${this.connectionTimeout}ms`));
      }, this.connectionTimeout);

      this.socket.on('data', (chunk: Buffer) => this.onData(chunk));
      this.socket.on('close', () => this.onDisconnect());
      this.socket.on('error', (err: Error) => this.onError(err));

      this.socket.connect(this.port, this.host, () => {
        clearTimeout(connectionTimer);
        this.connected = true;
        this.reconnectAttempts = 0;
        console.error(`[UnrealOpenCode] Connected to UE on ${this.host}:${this.port}`);
        resolve();
      });
    });
  }

  async sendRequest(type: string, params: Record<string, unknown> = {}): Promise<UEResponse> {
    if (!this.connected || !this.socket) {
      throw new Error('Not connected to Unreal Engine editor');
    }

    const request: UERequest = {
      id: randomUUID(),
      type,
      params,
      timestamp: Date.now(),
    };

    return new Promise<UEResponse>((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pendingRequests.delete(request.id);
        reject(new Error(`Request timeout: ${type} (${this.requestTimeout}ms)`));
      }, this.requestTimeout);

      this.pendingRequests.set(request.id, { resolve, reject, timeout });

      const encoded = this.encodeMessage(JSON.stringify(request));
      this.socket!.write(encoded);
    });
  }

  disconnect(): void {
    this.connected = false;
    // Reject all pending requests on deliberate disconnect
    for (const [id, pending] of this.pendingRequests) {
      clearTimeout(pending.timeout);
      pending.reject(new Error('Client disconnected'));
      this.pendingRequests.delete(id);
    }
    if (this.socket) {
      this.socket.removeAllListeners();
      this.socket.destroy();
      this.socket = null;
    }
    console.error('[UnrealOpenCode] Disconnected from UE');
  }

  isConnected(): boolean {
    return this.connected;
  }

  // ─── Message Framing ─────────────────────────────────────────

  /**
   * Encode a JSON string into a length-prefixed buffer.
   * Format: 4-byte big-endian uint32 (byte length) + UTF-8 JSON bytes.
   */
  private encodeMessage(json: string): Buffer {
    const jsonBuffer = Buffer.from(json, 'utf-8');
    const lengthBuffer = Buffer.allocUnsafe(4);
    lengthBuffer.writeUInt32BE(jsonBuffer.length, 0);
    return Buffer.concat([lengthBuffer, jsonBuffer]);
  }

  /**
   * Handle incoming TCP data chunks.
   * Accumulates into receiveBuffer and extracts complete messages
   * using the 4-byte length prefix framing protocol.
   */
  private onData(chunk: Buffer): void {
    this.receiveBuffer = Buffer.concat([this.receiveBuffer, chunk]);

    while (this.receiveBuffer.length >= 4) {
      const messageLength = this.receiveBuffer.readUInt32BE(0);

      if (this.receiveBuffer.length < 4 + messageLength) {
        // Not enough data yet — wait for more
        break;
      }

      const jsonBuffer = this.receiveBuffer.subarray(4, 4 + messageLength);
      this.receiveBuffer = this.receiveBuffer.subarray(4 + messageLength);

      const json = jsonBuffer.toString('utf-8');
      this.handleMessage(json);
    }
  }

  // ─── Message Handling ─────────────────────────────────────────

  /**
   * Parse incoming JSON, validate with UEResponseSchema, and match to pending request.
   * Errors are logged but never crash the client.
   */
  private handleMessage(json: string): void {
    let parsed: unknown;
    try {
      parsed = JSON.parse(json);
    } catch (err) {
      console.error('[UnrealOpenCode] Failed to parse response JSON:', err);
      return;
    }

    const result = UEResponseSchema.safeParse(parsed);
    if (!result.success) {
      console.error('[UnrealOpenCode] Invalid response schema:', result.error.message);
      return;
    }

    const response: UEResponse = result.data as UEResponse;
    const pending = this.pendingRequests.get(response.id);

    if (!pending) {
      console.error(`[UnrealOpenCode] No pending request for response id: ${response.id}`);
      return;
    }

    this.pendingRequests.delete(response.id);
    clearTimeout(pending.timeout);

    if (!response.success) {
      pending.reject(
        new Error(response.error?.message ?? `Request failed: ${response.type}`),
      );
    } else {
      pending.resolve(response);
    }
  }

  // ─── Connection Lifecycle ─────────────────────────────────────

  private onError(err: Error): void {
    console.error('[UnrealOpenCode] TCP error:', err.message);
  }

  private async onDisconnect(): Promise<void> {
    // Guard: if disconnect() was called deliberately, don't reconnect
    if (!this.connected && !this.socket) {
      return;
    }

    this.connected = false;
    this.socket = null;

    // Reject all pending requests
    for (const [id, pending] of this.pendingRequests) {
      clearTimeout(pending.timeout);
      pending.reject(new Error('Connection lost'));
      this.pendingRequests.delete(id);
    }

    // Auto-reconnect
    if (this.reconnectAttempts < this.maxReconnectAttempts) {
      this.reconnectAttempts++;
      console.error(
        `[UnrealOpenCode] Disconnected, reconnecting (${this.reconnectAttempts}/${this.maxReconnectAttempts})...`,
      );
      await new Promise((r) => setTimeout(r, this.reconnectInterval));
      try {
        await this.connect();
      } catch {
        // connect() failure triggers another onDisconnect cycle via socket events,
        // or reconnectAttempts will exhaust and we log final message below.
      }
    } else {
      console.error(
        '[UnrealOpenCode] Max reconnection attempts reached. UE editor may not be running.',
      );
    }
  }
}
