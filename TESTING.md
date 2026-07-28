# Testing

The executable contract test uses real `libpkgfetch` materialization and an
injected backend. It proves:

- exact source and package-input admission;
- source-object reopening, byte revalidation, and read-only staging;
- absence of inferred source extraction;
- denied-network, closed-environment execution translation;
- exact resource and package-input binding;
- complete execution-evidence retention;
- execution failure mapping into a failed build result;
- descriptor-based package-root inspection;
- regular, directory, symbolic-link, hard-link, and FIFO encoding;
- rejection of unsupported output objects;
- non-replacing artifact publication;
- cleanup after failed result sealing;
- independent inspection of the published bytes through `libpkgimage`;
- deterministic bytes and build identity across different effect paths;
- rejection of backend evidence for a different execution request.

Run the normal suite:

```sh
meson test -C build --print-errorlogs
```

Qualify each dependency closure independently:

```sh
./ci/qualify.sh shared
./ci/qualify.sh static
```
