# Project A: High-Performance Distributed Configuration & Feature Flag Engine

This directory contains the code for the self-hosted feature flag and remote configuration engine.

## Subprojects
- `server/`: The backend gRPC/HTTP server that manages and streams configuration rules.
- `client-sdk/`: The client library that apps import to check flags.
- `dashboard/`: A simple UI/CLI to manage config toggles.
