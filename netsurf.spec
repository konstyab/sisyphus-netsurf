Name: netsurf
Version: 3.6
Release: alt1

Summary: NetSurf is a free, open source web browser

License: GPLv2
Group: Networking/WWW
Url: http://netsurf-browser.org/

Packager: Sample Maintainer <samplemaintainer@altlinux.org>
BuildPreReq: libgtk+2-devel intltool libexpat-devel gperf bison flex libcurl-devel libssl-devel libjpeg-devel
Source: %name-%version.tar

%description
NetSurf is a free, open source web browser. It is written in
C and released under the GNU Public Licence version 2.
NetSurf has its own layout and rendering engine entirely
written from scratch. It is small and capable of handling
many of the web standards in use today

%prep
%setup

%build
sed -E -i.bak "s/^\\s*#include\\s*\"nsgenbind-lexer.h\"\\s*\$//g" nsgenbind/src/nsgenbind-parser.y
rm nsgenbind/src/nsgenbind-parser.y.bak -fv

%make_build



%install
%makeinstall_std
pushd %buildroot
mkdir -p usr
mv bin share usr/
popd

%find_lang %name

mkdir -p %buildroot/usr/share/applications
cat <<EOF > %buildroot/usr/share/applications/%name.desktop
[Desktop Entry]
Exec=netsurf-gtk %%u
Icon=/usr/share/%name/netsurf.xpm
Type=Application
Name=NetSurf
GenericName=Web-Browser
MimeType=text/html;x-scheme-handler/https;x-scheme-handler/http
StartupNotify=true
Categories=GTK;WebBrowser;Network;
EOF

%files -f %name.lang
%_bindir/*
/usr/share/%name
/usr/share/applications/%name.desktop

%changelog
* Sat Dec 03 2016 Sample Maintainer <samplemaintainer@altlinux.org> 3.6-alt1
- initial build

