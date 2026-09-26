#!/usr/bin/env python3
"""Loopback-only HTTP API for the local Weaver model and action protocol."""
from __future__ import annotations

import argparse
import json
import os
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from agent_protocol import PROTOCOL, ProtocolError
from model_runtime import DEFAULT_ADAPTER, DEFAULT_BASE, LocalWeaverModel, ProjectIndex, sanitize_scene_context


class State:
    model: LocalWeaverModel | None = None
    index: ProjectIndex | None = None
    started = time.time()


def handler_class(log_requests: bool):
    class Handler(BaseHTTPRequestHandler):
        server_version = "WeaverAgent/1"

        def _json(self, status: int, value: object) -> None:
            encoded = json.dumps(value, ensure_ascii=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(encoded)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(encoded)

        def do_GET(self) -> None:
            if self.path == "/health":
                self._json(200, {"ok": State.model is not None, "protocol": PROTOCOL["version"],
                                 "model_loaded": State.model is not None,
                                 "index_chunks": len(State.index.records) if State.index else 0,
                                 "uptime_seconds": round(time.time() - State.started, 1)})
            elif self.path == "/v1/tools":
                self._json(200, PROTOCOL)
            else:
                self._json(404, {"error": "not found"})

        def do_POST(self) -> None:
            if self.path != "/v1/agent/turn":
                self._json(404, {"error": "not found"})
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 128_000:
                    raise ValueError("request body must be between 1 and 128000 bytes")
                body = json.loads(self.rfile.read(length))
                if not isinstance(body, dict):
                    raise ValueError("request must be a JSON object")
                if "message" in body:
                    conversation = [{"role": "user", "content": body["message"]}]
                else:
                    conversation = body.get("conversation")
                if not isinstance(conversation, list) or len(conversation) > 32:
                    raise ValueError("conversation must contain at most 32 messages")
                scene_context = sanitize_scene_context(body.get("scene_context"))
                result = State.model.generate(conversation, State.index, scene_context=scene_context)
                self._json(200, {"protocol": PROTOCOL["version"], **result})
            except (ValueError, json.JSONDecodeError, ProtocolError) as exc:
                self._json(400, {"error": str(exc)})
            except Exception as exc:
                self._json(500, {"error": f"inference failed: {type(exc).__name__}: {exc}"})

        def log_message(self, format: str, *args: object) -> None:
            if log_requests:
                super().log_message(format, *args)

    return Handler


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1", help="Only loopback is accepted")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--base-model", type=Path, default=DEFAULT_BASE)
    parser.add_argument("--adapter", type=Path, default=DEFAULT_ADAPTER)
    parser.add_argument("--index", type=Path, default=Path(__file__).resolve().parents[2] / ".cache/weaverprocedura/project-index.jsonl")
    parser.add_argument("--bf16-inference", action="store_true", help="Use BF16 instead of 4-bit inference")
    parser.add_argument("--max-context", type=int, default=4096)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()
    if args.host not in {"127.0.0.1", "localhost", "::1"}:
        raise SystemExit("The initial Loom agent endpoint is loopback-only")
    if not args.adapter.is_dir():
        raise SystemExit(f"LoRA adapter not found: {args.adapter}; finish training first")
    State.index = ProjectIndex(args.index)
    print(f"Indexed {len(State.index.records)} local source chunks", flush=True)
    State.model = LocalWeaverModel(args.base_model, args.adapter,
                                   inference_4bit=not args.bf16_inference,
                                   max_context=args.max_context)
    server = ThreadingHTTPServer((args.host, args.port), handler_class(not args.quiet))
    print(f"Weaver agent API listening on http://{args.host}:{args.port}", flush=True)
    try:
        server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
