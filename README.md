# ReadWriteProxy

Since SSL sockets cannot be used as file handles in the creation of a cmd/powershell process, pipes can be used to read and write to the external process. This class aids that process.
