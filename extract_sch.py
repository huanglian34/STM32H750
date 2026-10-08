import fitz

doc = fitz.open("schdoc.pdf")
page = doc[0]
for text in page.get_text("blocks"):
    if any(k in text[4] for k in ["W25Q", "U3", "WP", "HOLD", "PE2", "PD13", "QSPI", "R"]):
        print(text[4].strip())
        print("-" * 30)
