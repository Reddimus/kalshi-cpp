# Security policy

`kalshi-cpp` is a third-party C++ client for the Kalshi exchange API.
It signs every request with an account-bound RSA private key, so a
vulnerability that mishandles credentials or leaks request material
could put live trading capital at risk. This file is the canonical
contact path for reporting one.

## Supported versions

Security fixes are made on the latest published `vX.Y.Z` tag. Older
tags are not back-patched. Bump your `FetchContent_Declare(... GIT_TAG ...)`
pin or your `find_package(kalshi X.Y.Z CONFIG REQUIRED)` constraint to
the latest minor on the same major as part of the upgrade.

| Version    | Supported          |
| ---------- | ------------------ |
| latest tag | :white_check_mark: |
| older      | :x:                |

## Reporting a vulnerability

**Do not open a public issue.** Use GitHub's [private vulnerability
reporting](https://github.com/Reddimus/kalshi-cpp/security/advisories/new)
flow, which delivers the report to the maintainer privately and
tracks coordinated disclosure.

When reporting, please include:

- Affected version (tag or commit SHA)
- A minimal reproduction or test case
- Impact (credential leak / request forgery / DoS / something else)
- Whether you've notified anyone else (e.g. Kalshi directly)

You can expect:

- Acknowledgement within **3 business days**
- An initial assessment + severity rating within **7 business days**
- A fix on a new `vX.Y.Z+1` tag, or a clear timeline if the fix is
  larger

## Out of scope

- Bugs against `kalshi.com` itself. Send those to Kalshi's own
  vulnerability program, not this client library.
- Operational issues such as rate-limit handling or network failures. File a
  regular issue.
- Theoretical issues against dependencies. Report them upstream to OpenSSL,
  libcurl, libwebsockets, Glaze, or GoogleTest as appropriate.
