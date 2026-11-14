# Copyright 2026 Ye Zicheng
# SPDX-License-Identifier: Apache-2.0

.PHONY: test e2e build headers

test:
	cargo test --workspace
	dotnet test hosts/windows/Linkspan.Protocol.Tests/Linkspan.Protocol.Tests.csproj
	javac -d /tmp/linkspan-java \
		hosts/android/protocol/src/main/java/app/linkspan/protocol/LaunchGate.java \
		hosts/android/protocol/src/test/java/app/linkspan/protocol/LaunchGateTest.java
	java -cp /tmp/linkspan-java app.linkspan.protocol.LaunchGateTest

e2e:
	./scripts/e2e-linux.sh

build:
	cargo build --workspace

headers:
	./scripts/check-headers.sh
