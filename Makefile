.PHONY: test-tools clean

test-tools:
	python3 -m unittest discover -s tests/tools -p "test_*.py" -v

clean:
	rm -rf build/
