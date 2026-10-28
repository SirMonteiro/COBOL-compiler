OUT := ../relatorio.pdf
MAX_CODE_COLS := 92

.PHONY: pdf clean

pdf:
	awk -v max=$(MAX_CODE_COLS) ' \
		/^<details>/ || /^<\/details>/ { next } \
		/^<summary>.*<\/summary>/ { sub(/^<summary>/, "### "); sub(/<\/summary>$$/, ""); print; next } \
		/^```/ { in_code = !in_code; print; next } \
		in_code && length($$0) > max { \
			s = $$0; \
			while (length(s) > max) { \
				print substr(s, 1, max); \
				s = "    " substr(s, max + 1); \
			} \
			print s; next \
		} \
		{ print }' ../README.md \
		| pandoc --citeproc --csl=associacao-brasileira-de-normas-tecnicas.csl \
			--bibliography=refs.bib -V geometry:margin=1.8cm -V lang=pt-BR -o $(OUT)

clean:
	rm -f $(OUT)
