Name: desk-rpm
Version: 1.0
Release: 1
Summary: Desk RPM
License: MIT
BuildArch: noarch

%description
A Linux program used to test RPM install on this desktop.

%install
mkdir -p %{buildroot}/usr/bin
install -m 755 %{_sourcedir}/desk-rpm %{buildroot}/usr/bin/desk-rpm

%files
/usr/bin/desk-rpm
