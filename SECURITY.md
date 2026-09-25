# Security policy

`kalshi-cpp` is a third-party C++ client for the Kalshi exchange API. It signs
requests with an account's Ed25519 or RSA private key, so a bug that leaks a
key or lets someone forge requests could put trading capital at risk.

## Supported versions

Fixes go into the next release after the latest tag; older releases are not
patched. Upgrade by moving your `GIT_TAG` or `find_package(kalshi X.Y CONFIG)`
version to the new release.

## Reporting a vulnerability

Don't open a public issue. Use GitHub's [private vulnerability
reporting](https://github.com/Reddimus/kalshi-cpp/security/advisories/new),
which reaches the maintainer privately and tracks disclosure.

Include:

- Affected version (tag or commit SHA)
- A minimal reproduction or test case
- Impact (credential leak / request forgery / DoS / something else)
- Whether you've notified anyone else (e.g. Kalshi directly)

You should hear back within 3 business days, and get an assessment within 7.
The fix ships in a new release, or you get a timeline if it takes longer.

## Out of scope

- Bugs against `kalshi.com` itself. Send those to Kalshi's own
  vulnerability program, not this client library.
- Operational issues such as rate-limit handling or network failures. File a
  regular issue.
- Theoretical issues against dependencies. Report them upstream to OpenSSL,
  libcurl, libwebsockets, Glaze, or GoogleTest as appropriate.
