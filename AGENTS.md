# Gipwrap Agent Guidelines

- Prefer `camelCase` naming for new symbols (functions, variables, files) when reasonable. Legacy code may still use other styles, but fresh additions should follow camelCase.
- Place standalone AI implementation source files under `src/aiImpl/`. Shared infrastructure belongs directly under `src/`.
- Keep build scripts updated to compile sources from `src/aiImpl/` when new integrations are added.
