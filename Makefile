# Copyright 2026 Ye Zicheng
# SPDX-License-Identifier: Apache-2.0

.PHONY: test e2e build headers

test:
	cargo test --workspace
	dotnet test hosts/windows/Olympium.Protocol.Tests/Olympium.Protocol.Tests.csproj
	javac -d /tmp/olympium-java \
		hosts/android/protocol/src/main/java/app/olympium/protocol/LaunchGate.java \
		hosts/android/protocol/src/test/java/app/olympium/protocol/LaunchGateTest.java
	java -cp /tmp/olympium-java app.olympium.protocol.LaunchGateTest

e2e:
	./scripts/e2e-linux.sh

build:
	cargo build --workspace

headers:
	./scripts/check-headers.sh
