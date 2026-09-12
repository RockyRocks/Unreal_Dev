# Shipping pipelines

| Document | Contents |
|---|---|
| [UE5 + Clang 18: PGO, ThinLTO, obfuscation, Sentry, Jenkins](ue5-clang18-pgo-lto-obfuscation-sentry-jenkins.md) | Step-by-step production recipe |
| [templates/](templates/) | `BuildConfiguration.xml`, `Target.cs`, `Jenkinsfile`, profile merge, Sentry upload, strip |

Copy templates into the game project and replace `MyGame` / engine paths. Do not enable IR obfuscation on Engine or on `HybridMessageRouter`.
