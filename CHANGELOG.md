# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),  
and this project adheres to [Semantic Versioning](https://semver.org/).

---

## [1.0.1] - 2025-09-04
### Added
- Embedded authorship signature in binary via `SMART_STORE_SIGNATURE`
- `Author` class with `getSignature()` method
- Logging integration for signature display

### Changed
- Updated `README.md` with authorship block and license declaration

---

## [1.0.0] - 2025-08-28
### Added
- Initial release of Smart_Store Framework
- Metadata-driven instantiation model
- Multi-format serialization (JSON, XML, binary)
- Undo/redo stack and schema migration logic