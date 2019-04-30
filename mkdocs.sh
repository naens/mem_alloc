#!/bin/sh
function robodoc_html ()
{
    mkdir -pv docs

    robodoc --src . --doc docs --html --multidoc \
         --nodesc --sections --tell --toc --index
    
    for f in docs/*.html
    do
        sed -i -e 's/toc_index.html/index.html/' "$f"
    done

    mv docs/toc_index.html docs/index.html

}

function robodoc_latex ()
{
    robodoc --src . --doc mem_alloc_docs --latex --singledoc \
        --sections --tell --toc --index
}

function robodoc_pdf()
{
    robodoc_latex
    mkdir -p tex
    mv mem_alloc_docs.tex tex
    cd tex
    pdflatex mem_alloc_docs.tex
    if [ -f "mem_alloc_docs.pdf" ]
    then
        mv mem_alloc_docs.pdf ../mem_alloc_docs.pdf
    fi
    cd ..
    rm -rf tex
}

if [ -z "$@" ]
then
    formats=html
else
    formats="$@"
fi

for var in "$formats"
do
    if [ "$var" = "html" ]
    then
        robodoc_html
    elif [ "$var" = "pdf" ]
    then
        robodoc_pdf
    elif [ "$var" = "latex" ]
    then
        robodoc_latex
    fi
done
