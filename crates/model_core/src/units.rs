//! Length units for display and input.
//!
//! The model always stores millimetres. A document has a display unit
//! (inches by default) used to show values and to read numbers typed without
//! a unit. Typed values may carry their own unit and mix them:
//! `2`, `2 in`, `2"`, `1/4`, `1 1/4 in`, `1' 6"`, `1ft 6in`, `25mm`,
//! `2in + 5mm`, `3 - 1/8`.

use std::fmt;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub enum LengthUnit {
    #[default]
    Inch,
    Foot,
    Millimeter,
    Centimeter,
    Meter,
}

impl LengthUnit {
    pub const ALL: [LengthUnit; 5] = [
        LengthUnit::Inch,
        LengthUnit::Foot,
        LengthUnit::Millimeter,
        LengthUnit::Centimeter,
        LengthUnit::Meter,
    ];

    pub fn mm_per_unit(self) -> f64 {
        match self {
            LengthUnit::Inch => 25.4,
            LengthUnit::Foot => 304.8,
            LengthUnit::Millimeter => 1.0,
            LengthUnit::Centimeter => 10.0,
            LengthUnit::Meter => 1000.0,
        }
    }

    pub fn symbol(self) -> &'static str {
        match self {
            LengthUnit::Inch => "in",
            LengthUnit::Foot => "ft",
            LengthUnit::Millimeter => "mm",
            LengthUnit::Centimeter => "cm",
            LengthUnit::Meter => "m",
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            LengthUnit::Inch => "Inches",
            LengthUnit::Foot => "Feet",
            LengthUnit::Millimeter => "Millimeters",
            LengthUnit::Centimeter => "Centimeters",
            LengthUnit::Meter => "Meters",
        }
    }

    pub fn is_imperial(self) -> bool {
        matches!(self, LengthUnit::Inch | LengthUnit::Foot)
    }

    /// Decimals shown when formatting (trailing zeros beyond two are trimmed).
    pub fn decimals(self) -> usize {
        match self {
            LengthUnit::Inch | LengthUnit::Centimeter => 3,
            LengthUnit::Foot | LengthUnit::Meter => 4,
            LengthUnit::Millimeter => 2,
        }
    }

    pub fn from_symbol(s: &str) -> Option<LengthUnit> {
        Some(match s.trim().to_ascii_lowercase().as_str() {
            "in" | "inch" | "inches" | "\"" | "″" => LengthUnit::Inch,
            "ft" | "foot" | "feet" | "'" | "′" => LengthUnit::Foot,
            "mm" | "millimeter" | "millimeters" | "millimetre" | "millimetres" => {
                LengthUnit::Millimeter
            }
            "cm" | "centimeter" | "centimeters" | "centimetre" | "centimetres" => {
                LengthUnit::Centimeter
            }
            "m" | "meter" | "meters" | "metre" | "metres" => LengthUnit::Meter,
            _ => return None,
        })
    }

    pub fn to_mm(self, value: f64) -> f64 {
        value * self.mm_per_unit()
    }

    pub fn from_mm(self, mm: f64) -> f64 {
        mm / self.mm_per_unit()
    }
}

impl fmt::Display for LengthUnit {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.symbol())
    }
}

/// `mm` shown in `unit`, e.g. `1.25 in`, `0.375 in`, `25.40 mm`.
pub fn format_length(mm: f64, unit: LengthUnit) -> String {
    let v = unit.from_mm(mm);
    let mut s = format!("{:.*}", unit.decimals(), v);
    if let Some(dot) = s.find('.') {
        while s.len() > dot + 3 && s.ends_with('0') {
            s.pop();
        }
    }
    if s == "-0.00" {
        s = "0.00".into();
    }
    format!("{s} {}", unit.symbol())
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ParseError(pub String);

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for ParseError {}

/// Reads a typed length and returns millimetres. Numbers without a unit are
/// in `default_unit`.
pub fn parse_length(text: &str, default_unit: LengthUnit) -> Result<f64, ParseError> {
    let tokens = tokenize(text)?;
    if tokens.is_empty() {
        return Err(ParseError("enter a length".into()));
    }
    let mut total = 0.0;
    let mut sign = 1.0;
    let mut i = 0;
    let mut expect_term = true;
    while i < tokens.len() {
        match &tokens[i] {
            Token::Plus | Token::Minus if expect_term && i > 0 => {
                return Err(ParseError("two operators in a row".into()));
            }
            Token::Plus => {
                sign = 1.0;
                expect_term = true;
                i += 1;
            }
            Token::Minus => {
                sign = -1.0;
                expect_term = true;
                i += 1;
            }
            Token::Unit(u) => return Err(ParseError(format!("'{u}' needs a number before it"))),
            Token::Slash => {
                return Err(ParseError("a fraction needs numbers on both sides".into()))
            }
            Token::Number(_) => {
                // number [number '/' number] | number '/' number, then optional unit.
                let (value, used) = read_quantity(&tokens[i..])?;
                i += used;
                let unit = match tokens.get(i) {
                    Some(Token::Unit(u)) => {
                        i += 1;
                        LengthUnit::from_symbol(u)
                            .ok_or_else(|| ParseError(format!("unknown unit '{u}'")))?
                    }
                    _ => default_unit,
                };
                total += sign * unit.to_mm(value);
                // A following term without an operator adds (1' 6", 1ft 6in).
                sign = 1.0;
                expect_term = false;
            }
        }
    }
    if expect_term {
        return Err(ParseError("expression ends with an operator".into()));
    }
    if !total.is_finite() {
        return Err(ParseError("not a number".into()));
    }
    Ok(total)
}

#[derive(Clone, Debug, PartialEq)]
enum Token {
    Number(f64),
    Unit(String),
    Plus,
    Minus,
    Slash,
}

fn tokenize(text: &str) -> Result<Vec<Token>, ParseError> {
    let mut out = Vec::new();
    let chars: Vec<char> = text.chars().collect();
    let mut i = 0;
    while i < chars.len() {
        let c = chars[i];
        if c.is_whitespace() {
            i += 1;
        } else if c.is_ascii_digit()
            || c == '.'
            || c == ',' && i + 1 < chars.len() && chars[i + 1].is_ascii_digit()
        {
            let start = i;
            while i < chars.len()
                && (chars[i].is_ascii_digit() || chars[i] == '.' || chars[i] == ',')
            {
                i += 1;
            }
            let raw: String = chars[start..i].iter().collect::<String>().replace(',', ".");
            let n = raw
                .parse::<f64>()
                .map_err(|_| ParseError(format!("'{raw}' is not a number")))?;
            out.push(Token::Number(n));
        } else if c == '+' {
            out.push(Token::Plus);
            i += 1;
        } else if c == '-' || c == '−' {
            out.push(Token::Minus);
            i += 1;
        } else if c == '/' {
            out.push(Token::Slash);
            i += 1;
        } else if c == '"' || c == '\'' || c == '″' || c == '′' {
            out.push(Token::Unit(c.to_string()));
            i += 1;
        } else if c.is_alphabetic() {
            let start = i;
            while i < chars.len() && chars[i].is_alphabetic() {
                i += 1;
            }
            out.push(Token::Unit(chars[start..i].iter().collect()));
        } else {
            return Err(ParseError(format!("unexpected '{c}'")));
        }
    }
    Ok(out)
}

/// `a`, `a/b` or `a b/c` at the start of `tokens`; returns (value, tokens used).
fn read_quantity(tokens: &[Token]) -> Result<(f64, usize), ParseError> {
    let num = |t: Option<&Token>| match t {
        Some(Token::Number(n)) => Some(*n),
        _ => None,
    };
    let a = num(tokens.first()).expect("caller checked");
    // a b/c (mixed number)
    if let (Some(b), Some(Token::Slash), Some(c)) =
        (num(tokens.get(1)), tokens.get(2), num(tokens.get(3)))
    {
        if c == 0.0 {
            return Err(ParseError("division by zero".into()));
        }
        return Ok((a + b / c, 4));
    }
    // a/b
    if let (Some(Token::Slash), Some(b)) = (tokens.get(1), num(tokens.get(2))) {
        if b == 0.0 {
            return Err(ParseError("division by zero".into()));
        }
        return Ok((a / b, 3));
    }
    if matches!(tokens.get(1), Some(Token::Slash)) {
        return Err(ParseError("a fraction needs numbers on both sides".into()));
    }
    Ok((a, 1))
}

#[cfg(test)]
mod tests {
    use super::*;
    use LengthUnit::*;

    fn mm(text: &str, unit: LengthUnit) -> f64 {
        parse_length(text, unit).unwrap_or_else(|e| panic!("{text:?}: {e}"))
    }

    fn near(a: f64, b: f64) -> bool {
        (a - b).abs() < 1e-9
    }

    #[test]
    fn inches_are_the_default() {
        assert_eq!(LengthUnit::default(), Inch);
    }

    #[test]
    fn plain_numbers_use_the_document_unit() {
        assert!(near(mm("2", Inch), 50.8));
        assert!(near(mm("2", Millimeter), 2.0));
        assert!(near(mm("0.5", Foot), 152.4));
        assert!(near(mm("1,5", Centimeter), 15.0));
    }

    #[test]
    fn explicit_units_override_the_document_unit() {
        assert!(near(mm("25mm", Inch), 25.0));
        assert!(near(mm("25 mm", Inch), 25.0));
        assert!(near(mm("2\"", Millimeter), 50.8));
        assert!(near(mm("2 in", Millimeter), 50.8));
        assert!(near(mm("1 inch", Millimeter), 25.4));
        assert!(near(mm("3 ft", Millimeter), 914.4));
        assert!(near(mm("1.2 m", Inch), 1200.0));
        assert!(near(mm("4cm", Inch), 40.0));
    }

    #[test]
    fn fractions_and_mixed_numbers() {
        assert!(near(mm("1/4", Inch), 6.35));
        assert!(near(mm("3/8\"", Millimeter), 9.525));
        assert!(near(mm("1 1/4", Inch), 31.75));
        assert!(near(mm("1 1/4 in", Millimeter), 31.75));
    }

    #[test]
    fn feet_and_inches_add_up() {
        assert!(near(mm("1' 6\"", Millimeter), 457.2));
        assert!(near(mm("1ft 6in", Millimeter), 457.2));
        assert!(near(mm("2' 3 1/2\"", Millimeter), (24.0 + 3.5) * 25.4));
    }

    #[test]
    fn simple_sums_and_differences() {
        assert!(near(mm("2in + 5mm", Inch), 55.8));
        assert!(near(mm("3 - 1/8", Inch), (3.0 - 0.125) * 25.4));
        assert!(near(mm("-1", Inch), -25.4));
    }

    #[test]
    fn errors_are_explained() {
        for bad in [
            "",
            "abc",
            "in",
            "2 parsecs",
            "1/0",
            "2 +",
            "1 / ",
            "3 + + 4",
            "2$",
        ] {
            assert!(
                parse_length(bad, Inch).is_err(),
                "{bad:?} should be rejected"
            );
        }
        assert_eq!(
            parse_length("2 parsecs", Inch).unwrap_err().0,
            "unknown unit 'parsecs'"
        );
    }

    #[test]
    fn formatting() {
        assert_eq!(format_length(25.4, Inch), "1.00 in");
        assert_eq!(format_length(9.525, Inch), "0.375 in");
        assert_eq!(format_length(31.75, Inch), "1.25 in");
        assert_eq!(format_length(25.4, Millimeter), "25.40 mm");
        assert_eq!(format_length(457.2, Foot), "1.50 ft");
        assert_eq!(format_length(15.0, Centimeter), "1.50 cm");
        assert_eq!(format_length(-0.0001, Inch), "0.00 in");
    }

    #[test]
    fn format_parse_round_trip() {
        for unit in LengthUnit::ALL {
            for v in [0.5, 6.35, 25.4, 123.456, 1000.0] {
                let text = format_length(v, unit);
                let back = parse_length(&text, Inch).unwrap();
                let tol = unit.mm_per_unit() * 10f64.powi(-(unit.decimals() as i32));
                assert!((back - v).abs() <= tol, "{v} mm → {text} → {back}");
            }
        }
    }
}
