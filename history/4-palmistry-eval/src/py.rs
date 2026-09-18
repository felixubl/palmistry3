#[cfg(feature = "python")]
use pyo3::prelude::*;

#[cfg(feature = "python")]
use pyo3::{exceptions::PyValueError, types::PyList};

#[cfg(feature = "python")]
use crate::{cards::encode_str, equity::{Player, Config, compute_equity}};

#[cfg(feature = "python")]
#[pyclass]
struct PyEquityResult {
    #[pyo3(get)]
    wins: Vec<u64>,
    #[pyo3(get)]
    ties: Vec<u64>,
    #[pyo3(get)]
    losses: Vec<u64>,
    #[pyo3(get)]
    samples: u64,
    #[pyo3(get)]
    equities: Vec<f64>,
}

#[cfg(feature = "python")]
#[pymodule]
fn poker_eval(py: Python<'_>, m: &PyModule) -> PyResult<()> {
    /// Compute equities.
    ///
    /// Args:
    ///   holes: list of per-player hole cards. Each item:
    ///          - ['As','Kd'] (two strings) or None (unknown)
    ///   board: list like ['7h','8h','9h'] length 0..5
    ///   monte_carlo: bool
    ///   mc_runs: int
    ///   seed: int|None
    ///   parallel: bool (requires building with feature 'parallel')
    #[pyfn(m)]
    #[pyo3(name = "equity")]
    fn equity_py(
        _py: Python<'_>,
        holes: &PyList,
        board: &PyList,
        monte_carlo: bool,
        mc_runs: u64,
        seed: Option<u64>,
    ) -> PyResult<PyEquityResult> {
        let mut players: Vec<Player> = Vec::with_capacity(holes.len());
        for item in holes {
            if item.is_none() {
                players.push(Player { hole: None, active: true });
            } else {
                let arr: Vec<String> = item.extract()?;
                if arr.len() != 2 {
                    return Err(PyValueError::new_err("Each known hole must be 2 cards"));
                }
                let c1 = encode_str(&arr[0]);
                let c2 = encode_str(&arr[1]);
                players.push(Player { hole: Some([c1, c2]), active: true });
            }
        }
        let mut bvec = Vec::with_capacity(board.len());
        for b in board {
            let s: String = b.extract()?;
            bvec.push(encode_str(&s));
        }

        let cfg = Config {
            monte_carlo,
            mc_runs,
            seed,
            ..Default::default()
        };
        let res = compute_equity(&players, &bvec, cfg);
        let eq = res.equities();
        Ok(PyEquityResult {
            wins: res.players.iter().map(|p| p.wins).collect(),
            ties: res.players.iter().map(|p| p.ties).collect(),
            losses: res.players.iter().map(|p| p.losses).collect(),
            samples: res.samples,
            equities: eq,
        })
    }

    Ok(())
}
