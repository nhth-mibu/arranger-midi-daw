# Public releases

The source repository stays private. The binaries are published in a separate
public repository:

<https://github.com/nhth-mibu/lazy-arranger-midi-daw-releases>

The public repository holds a README, release notes and release assets only. It
does not hold the source code. GitHub creates "Source code" archives for its
tags, but those archives contain only the public README.

## How a release is published

1. Build and verify a release in the private repository.
2. Publish the release in the private repository.
3. The `Publish public release` workflow starts when the private release is
   published.
4. It downloads the private release assets, checks every `.sha256` file, and
   creates the same tag in the public repository.
5. It uploads the packages and checksums, then downloads them again from the
   public repository and checks them.

The workflow refuses to replace an existing public release. If a run fails
after creating the release, fix the cause and publish a new version; do not
delete a published release without a reason.

## Set up the token

The private workflow needs a token that can write releases in the public
repository. Use a fine-grained token, not a classic token with access to every
repository.

1. Open GitHub `Settings > Developer settings > Personal access tokens >
   Fine-grained tokens > Generate new token`.
2. Resource owner: `nhth-mibu`.
3. Repository access: only `lazy-arranger-midi-daw-releases`.
4. Permissions: `Contents: Read and write`. `Metadata: Read` is added
   automatically.
5. Set an expiry date and create the token.
6. In the private repository, open `Settings > Secrets and variables > Actions`
   and add a repository secret named `PUBLIC_RELEASES_TOKEN` with the token.

The workflow uses the token only for the public repository. The private release
download uses the workflow's own `github.token`.

Rotate the token before it expires. If the secret is missing, the workflow
stops with a clear error before it changes the public repository.

## Workflow

[`publish-public-release.yml`](../.github/workflows/publish-public-release.yml)

It runs on:

```yaml
on:
  release:
    types: [published]
```

The public repository is set in `PUBLIC_REPO`. Change it in one place if the
repository moves.

## Verify by hand

```sh
gh release view v0.0.16 \
  --repo nhth-mibu/lazy-arranger-midi-daw-releases \
  --json url,assets

curl -I https://github.com/nhth-mibu/lazy-arranger-midi-daw-releases/releases/latest
```

A signed-out browser must be able to open the release and download the assets.
