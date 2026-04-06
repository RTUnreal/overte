use itertools::Itertools;

#[cxx::bridge(namespace = "image::rust")]
mod ffi {
    enum ResizeFilter {
        Fast,
        Smooth,
    }

    #[derive(Debug, Clone, Copy)]
    struct Pixel {
        pub red: f32,
        pub blue: f32,
        pub green: f32,
        pub alpha: f32,
    }

    struct MipMapResult {
        data: Vec<Pixel>,
        width: usize,
        height: usize,
    }

    // Rust types and signatures exposed to C++.
    extern "Rust" {
        fn scale_float_image(
            input: &CxxVector<Pixel>,
            width: usize,
            height: usize,
            desired_width: usize,
            desired_height: usize,
            filter: ResizeFilter,
        ) -> Vec<Pixel>;

        type MipMapBuilder;
        #[Self = "MipMapBuilder"]
        fn create(input: &CxxVector<Pixel>, width: usize, height: usize) -> Box<MipMapBuilder>;
        fn can_build_next_mip_map(&self) -> bool;
        fn build_next_mip_map(&mut self) -> bool;
        fn get_mipmap(&self) -> MipMapResult;
    }

    // C++ types and signatures exposed to  Rust.
    unsafe extern "C++" {}
}

fn scale_float_image(
    input: &cxx::CxxVector<ffi::Pixel>,
    width: usize,
    height: usize,
    desired_width: usize,
    desired_height: usize,
    filter: ffi::ResizeFilter,
) -> Vec<ffi::Pixel> {
    assert!(input.len() == width * height);

    // we need to translate it and not use ffi::Pixel directly due to
    // the requirements of the `image::Pixel` trait.
    let translated_image = input
        .iter()
        .map(|p| [p.red, p.green, p.blue, p.alpha])
        .flatten()
        .collect::<Vec<_>>();

    let image = image::ImageBuffer::<image::Rgba<f32>, _>::from_vec(
        width as u32,
        height as u32,
        translated_image,
    )
    .expect("image to be valid");

    let image = image::imageops::resize(
        &image,
        desired_width as u32,
        desired_height as u32,
        match filter {
            ffi::ResizeFilter::Fast => {
                // actually a box filter
                image::imageops::FilterType::Nearest
            }
            ffi::ResizeFilter::Smooth => image::imageops::FilterType::Lanczos3,
            _ => unreachable!(),
        },
    );

    image
        .iter()
        .chunks(4)
        .into_iter()
        .map(|mut p| {
            let a = p.next_array::<4>().expect("should be 4");
            assert_eq!(p.next(), None);
            ffi::Pixel {
                red: *a[0],
                blue: *a[1],
                green: *a[2],
                alpha: *a[3],
            }
        })
        .collect::<Vec<_>>()
}

struct MipMapBuilder {
    current_image: image::ImageBuffer<image::Rgba<f32>, Vec<f32>>,
}

impl MipMapBuilder {
    fn create(input: &cxx::CxxVector<ffi::Pixel>, width: usize, height: usize) -> Box<Self> {
        assert!(input.len() == width * height);
        // we need to translate it and not use `ffi::Pixel` directly due to
        // the requirements of the `image::Pixel` trait.
        let translated_image = input
            .iter()
            .map(|p| [p.red, p.green, p.blue, p.alpha])
            .flatten()
            .collect::<Vec<_>>();

        let current_image = image::ImageBuffer::<image::Rgba<f32>, _>::from_vec(
            width as u32,
            height as u32,
            translated_image,
        )
        .expect("image to be valid");

        Box::new(Self { current_image })
    }
    fn can_build_next_mip_map(&self) -> bool {
        self.current_image.width() > 1 && self.current_image.height() > 1
    }

    fn build_next_mip_map(&mut self) -> bool {
        if !self.can_build_next_mip_map() {
            return false;
        }

        let current_image = &self.current_image;
        self.current_image = image::imageops::resize(
            current_image,
            current_image.width() / 2,
            current_image.height() / 2,
            image::imageops::FilterType::Nearest,
        );
        true
    }

    fn get_mipmap(&self) -> ffi::MipMapResult {
        ffi::MipMapResult {
            data: self
                .current_image
                .iter()
                .chunks(4)
                .into_iter()
                .map(|mut p| {
                    let a = p.next_array::<4>().expect("should be 4");
                    assert_eq!(p.next(), None);
                    ffi::Pixel {
                        red: *a[0],
                        blue: *a[1],
                        green: *a[2],
                        alpha: *a[3],
                    }
                })
                .collect::<Vec<_>>(),
            width: self.current_image.width() as usize,
            height: self.current_image.height() as usize,
        }
    }
}
