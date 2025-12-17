import zipfile

def create_test_archive(filename, num_files):
    print(f"Creating {filename} with {num_files} files...")
    with zipfile.ZipFile(filename, 'w', zipfile.ZIP_STORED) as zf:
        for i in range(num_files):
            zf.writestr(f"file_{i:05d}.txt", f"This is content of file {i}")
    print("Done.")

if __name__ == "__main__":
    create_test_archive("test_perf.zip", 10000)
