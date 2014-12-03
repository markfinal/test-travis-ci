#! /bin/bash

set -e
set -x

hash=$( git log -1 --format="%h")

root="$PWD"
mkdir -p tmp/$$
cd tmp/$$

# grab doc
cp -R $root/*doc .

# delete gitignore files
rm */.gitignore

# This file should be here, it is needed otherwise
# files beginning with _ are skipped
touch .nojekyll

cat > index.html << EOF
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0 Transitional//EN">
<html>
<head>
<title>Page Auto Redirect</title>
<meta http-equiv="refresh" content ="0; user-doc/html/index.html">
</head>
<body>
This is an auto redirect page.
</body>
</html>
EOF

git init
git config user.email "giovanni.bussi+plumedbot@gmail.com"
git config user.name "plumedbot"
git checkout -b gh-pages
git remote add doc https://plumedbot:$GIT_TOKEN@github.com/GiovanniBussi/test-plumed-doc.git
git add --all .
git commit -m "Update to plumed/plumed2@$hash"
git push -q -f doc gh-pages
