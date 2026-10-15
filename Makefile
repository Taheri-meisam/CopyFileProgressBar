CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

copy-progress: CopyFileProgressBar/CopyFileProgressBar.cpp CopyFileProgressBar/fileHeader.h
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f copy-progress

.PHONY: clean
