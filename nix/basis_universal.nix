{
  stdenv,
  fetchFromGitHub,
  fetchpatch,
  cmake,

  zstd,
}:
stdenv.mkDerivation {
  name = "basis_universal";
  version = "2.1.0";
  src = fetchFromGitHub {
    owner = "BinomialLLC";
    repo = "basis_universal";
    tag = "v2_1_0";
    hash = "sha256-zBpfWi8hieviLtbnIM1AcXqnB6rd1ta/v+one6dEXGA=";
  };

  patches = [
    (fetchpatch {
      url = "https://github.com/BinomialLLC/basis_universal/commit/8c4c65b6ed079a3a012a895b10d044fd9452dea5.patch";
      hash = "sha256-ksISAzxyEXnTs6/wVjLYX3C/rDctgxdNmGCNqPD8FQM=";
    })
  ];

  nativeBuildInputs = [ cmake ];

  buildInputs = [ zstd ];
}
