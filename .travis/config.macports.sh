#! /bin/bash

set -e
set -x

cd macports

cp /opt/local/etc/macports/sources.conf $$
awk -v repo="file://$PWD" '{if($NF=="[default]") print repo; print}' $$ > $$.new
sudo mv -f $$.new /opt/local/etc/macports/sources.conf
echo "** NEW SOURCES"
cat /opt/local/etc/macports/sources.conf
echo "** END NEW SOURCES"
rm -f $$

cp /opt/local/etc/macports/macports.conf $$
awk '{if($1=="configureccache") print "configureccache yes" ; else print }' $$ > $$.new
sudo mv -f $$.new /opt/local/etc/macports/macports.conf
cat "** NEW CONFIG"
cat /opt/local/etc/macports/macports.conf
cat "** END NEW CONFIG"
rm -f $$

portindex

