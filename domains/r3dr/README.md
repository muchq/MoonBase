# r3dr Domain

URL shortening services and related applications.

## APIs

- [**r3dr_v2 API**](apis/r3dr_v2): The C++ shortener on smithy-cpp (#1359),
  serving `/r3dr/v1/*` behind `api.muchq.com`. Where new work lands.
- [**r3dr API**](apis/r3dr): The original Go service, still serving
  `r3dr.net` on its own storage until deprecation.

## Apps

- [**r3dr Web**](apps/r3dr_web): The `r3dr.net` frontend, retiring with the
  domain; its replacement is a `muchq.com` page (#1359 chunk 2, in the
  muchq.github.io repo).
