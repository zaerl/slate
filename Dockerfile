# The Slate library
#
# This file is distributed under the MIT License. See LICENSE for details.

FROM alpine

RUN apk add --no-cache build-base

WORKDIR /slate

COPY Makefile ./
COPY src/ src/
COPY tests/ tests/

RUN make bin/test bin/test-cpp

CMD ["make", "test", "test-cpp"]
